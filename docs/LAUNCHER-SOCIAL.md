# Launcher social client (SOCIAL-1 S-D)

This is the private launcher's client of `docs/SOCIAL-API.md`, pinned to the
contract-first S-A revision `45d495043d2e5b6a5a95d8c6aea7ec878da1abb0`.
The contract is an implementation target, not evidence of a deployed service.

## Using it

Open Multiplayer and sign in with a player name. Sign-in is explicit: merely
opening the launcher does not register an install or create a social session.
The configured lobby is the existing launcher `--lobby=https://host` setting.
Offline and UI-demo modes disable sign-in, including OAuth.

Discord and e-mail controls use the SOCIAL-2b extension in `SOCIAL-API.md` and docs/IDENTITY.md. While
signed in they link/unlink the provider to the current S-A PID and roster
session; **Check linked accounts** refreshes the unlink controls. While signed
out they restore a previously linked player using this PC's shared install,
without first allocating a guest PID. Restore must precede ordinary sign-in
on a new PC: an install already bound to a different player is not merged.
No `identity_key.txt` is created or read. Success adopts the returned social
token and reloads the existing friends/inbox; invite Join still uses the same
game-owned handoff, never an OAuth credential or token on argv.

Discord sign-in uses the fixed loopback callback
`http://127.0.0.1:43827/oauth/callback`. Port conflicts fail closed. Cancel,
sign-out and origin changes close the listener and clear pending secrets.
E-mail sign-in instead opens the lobby's `/email/<ticket>` page and polls
`/v1/auth/email/poll` with an in-memory secret; it needs no loopback listener.
Provider errors are neutral; no provider response or secret is shown/logged.
Owner-provisioned confidential apps and Worker secrets remain necessary:
Discord scope `identify` (Google dropped, 2026-10-20), and
`OAUTH_DISCORD_{CLIENT_ID,CLIENT_SECRET}`. No live provider or deployed
Worker validation is implied by the mock tests.

The friends panel shows presence and supports friend-code copy, code requests,
accept/decline, removal and friend chat. Requests distinguish incoming and
outgoing entries. Invites/challenges appear both in the Invites tab and in a
toast on other launcher screens; Later dismisses the toast without declining.
Expired or removed invitations disappear. Chat history is paged, with at most
200 loaded messages displayed; client display limits do not delete history.

Recovery code generation replaces the old code and shows the new secret once.
It is never saved by the launcher. The sign-in form's recovery option redeems
a code on a different, unbound install; it cannot merge existing accounts.

## Identity and game launch

The identity is `GameOptions.game_user_dir()/install_id.txt`, the game's file,
not the launcher's `user://` directory. `OPENBFME_GAME_USER_DIR` retains the
existing portable/test override. Invalid files fail closed without regeneration.
The launcher acquires the game-directory `.install_id.lock` directory before
reading or creating the file; contention reports a retryable failure. The game
no longer uses that lock: it writes a temporary file, renames it into place and
reads the resulting `install_id.txt`. Both reuse an existing valid file. The
launcher's lock does not serialize creation by a concurrently starting game.

The same in-memory lobby token is inherited by the verified installed game via
`OPENBFME_LOBBY_TOKEN`; it is never an argument, saved preference, URL or log
entry. The launcher restores its previous environment immediately after the
spawn attempt, including failures. Both installed-version and bundled-game
launches run the existing verification gates before the token is supplied.

Join passes `--online-invite=<16 lowercase hex>` and the configured lobby to
S-B/C's game bridge. Only the game can compute its mounted content profile.
It accepts the invitation, then uses the returned room ID with the existing
ordinary join path and **the same token**. The launcher never copies the host's
profile or accepts/reserves a room itself. Game-side acceptance failures are
reported by the game. A successful process spawn is not proof of a successful
room join. The launcher stops its authenticated polling after handing off.

This requires a game build containing S-B/C's invite bridge and the shared
`install_id.txt` identity. The original WP-C game does not understand the pending-invite argument.
Room-code routes from ONLINEHUB-1 are not assumed: the frozen contract uses
existing password rooms and recipient-bound invitation grants.

## Network and privacy boundaries

Production uses the configured HTTPS origin only. Explicit development/test
configuration can allow one HTTP loopback origin; release builds cannot opt in
through production arguments. Redirects never forward a bearer token. Tokens
and recovery secrets live only in memory. Failures show named, neutral client
errors rather than raw upstream text.

One sequential inbox request holds for up to 25 seconds. Every response replaces
the previous arrays, even with `reset:false`; its cursor is opaque. Server
backoff and rate limits pause polling. There is no additional status, chat or
authenticated stats timer. While signed in, the existing players-online chip
uses inbox stats instead of anonymous stats requests. Signing out cancels
pending requests and clears identity, contacts, chat and notifications from UI.

## Verification boundary

`tools/release/test_launcher_social.py` exercises the actual asynchronous client
against a loopback stand-in using the frozen shapes. The launch helper test is
`launcher/tests/social_launch_test.gd`; it verifies arguments, inherited token,
prelaunch refusal and environment restoration without running retail content.
The existing launcher layout and release/verification tests remain applicable.

Stand-in tests do not validate S-A persistence/enforcement or S-B/C's mounted
profile, actual invitation acceptance and reserved-slot join. Those need a
combined compatible lobby/game build and remain the S-E integration gate.
