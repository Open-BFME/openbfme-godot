# Lobby OAuth owner setup

This is the SOCIAL-2b implementation and supersedes the credential/PID model
described by the original PR #5. OAuth links Discord (ONLINEMENU-1: Google dropped by the owner; e-mail sign-in in docs/IDENTITY.md) to the existing
S-A player record. It does not create a second identity, move social data, or
merge players.

## Provider applications

The callback URI is fixed:

```text
http://127.0.0.1:43827/oauth/callback
```

Create a Discord **confidential application** and enable the `identify` scope.
Configure its client ID and client secret as Worker secrets named
`OAUTH_DISCORD_CLIENT_ID` and `OAUTH_DISCORD_CLIENT_SECRET`.

(Google was dropped by the owner on 2026-10-20; e-mail sign-in replaces it: docs/IDENTITY.md.)

Configure each provider's ID and secret together. A partial pair fails startup
configuration. Secrets are runtime configuration; do not place them in source,
command arguments, URLs, or logs.

## Identity behavior

Restore is started before guest sign-in and does not allocate an identity.
After provider verification it rebinds the existing stable 16-hex PID to the
new install, preserving the existing code, privacy settings, friend/block
graph, and permanent chat. A restored session replaces the old install-bound
session. If an account already exists on the new install, sign in with it
normally instead of attempting restore.

Restore keeps the player's recovery code and tells the install it left (a
one-time sign-in notice), so a restore the owner did not make can be undone
with that code.

Link keeps the current roster account ID and session identity. Unlink removes
only that provider association; it does not revoke the local session, remove
social data, or change the install binding. Link and unlink must name the
session's install (session tokens carry it sealed, so a stolen token alone is
not enough); a linked provider is never replaced, and unlinking or linking a
second provider also needs the player's recovery code (ONLINEMENU-1, PR #8
review finding 2). Keep the lobby signing key stable
across restarts: `SESSION_KEY` HMACs both session tokens and provider-subject
and recovery indexes. Rotating it breaks those lookups and requires a deliberate
migration/relink plan; this implementation does not provide key migration.
Preserve the shared social store as well: provider subject indexes and OAuth
mutation metadata are stored in its `oauth2:` namespace.

Provider calls use fixed HTTPS endpoints, no redirects, a 64 KiB response cap,
and a five-second timeout. The test suite injects mock provider responses and
tests local loopback/contract behavior. Passing those tests is **not** live
Discord app qualification; real provider setup and consent must be
validated by the owner in the intended deployment environment.
