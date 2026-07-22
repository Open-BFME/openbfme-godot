# Security policy

OpenBFME is experimental software that processes a proprietary local game
installation and untrusted content containers. Security reports are welcome even
before the first supported release.

## Report privately

Use GitHub's private security-advisory reporting for this repository when it is
available. Do not open a public issue containing exploit details, credentials,
private paths, retail payloads, or proof-of-concept files that may redistribute
game content.

If private reporting is unavailable, open a minimal issue asking the maintainer
for a private contact channel without including sensitive details.

## High-priority reports

- Retail or converted retail content escaping the private workspace.
- Archive extraction, path traversal, symlink, junction, or reparse-point escape.
- Unsafe external-tool execution or argument injection.
- Secrets, credentials, tokens, or private paths entering an export.
- Content packs loading executable code outside their declared boundary.
- Multiplayer input that can crash, hang, corrupt, or escape the server process.
- Update, rollback, or pack-selection behavior that can replace known-good state
  with unverified content.

## Include

- affected revision and operating system;
- exact minimal reproduction;
- expected and observed behavior;
- impact and whether retail payloads may have escaped;
- relevant sanitized logs; and
- suggested correction, if known.

Do not attach a BFME installation, extracted source entry, converted retail asset,
private pack, or original-game capture. Use synthetic fixtures or describe the
private evidence without transmitting it.

## Supported versions

No public release is currently supported. Reports against the latest public
default branch are still useful. A formal version-support table will be added
when the project ships its first release.
