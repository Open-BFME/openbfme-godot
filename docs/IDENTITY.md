# Player identity (lane ONLINEMENU-1, the owner's decisions of 2026-10-20)

Owner: "lets keep this simple lets have the email + hardware id and people can just play will adjust if it becomes an issue" and
"include discord sign in". This replaces SOCIAL-1 §8.2's Discord-or-Google plan; the recovery code stays.

## What a player has
- **Everyone just plays.** No sign-up, no gates. Identity is the install (a random id made once per installation; session tokens
  carry it sealed since PR #8's fix, so a token is never a bearer install id) and, optionally, a recovery code.
- **Hardware id, for bans only:** a one-way SHA-256 of `openbfme-device-v1`, a zero byte and the OS machine id (Windows
  `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid`; macOS IOPlatformUUID and Linux `/etc/machine-id` through Godot's
  `OS.get_unique_id`), made on the client (`launcher/scripts/core/social_identity.gd`, `godot/scripts/online_identity.gd`) and sent
  only as the hash (`device`). Never shown to players or peers. It is not hard to fake (accepted by the owner).
- **Account across PCs (optional):** Discord sign-in (OAuth, scope `identify`; the lobby keeps an HMAC of the Discord user id) or a
  confirmed e-mail address (a magic sign-in link; the lobby keeps an HMAC fingerprint, never the address). Either can move the player
  to another PC; a second one needs the recovery code; a linked one is never silently replaced; unlinking needs the recovery code
  (PR #8's fixes). A linked player who lost the recovery code makes a new one by confirming with a linked identity.

## Bans
A ban (moderation's enforcement record of an install) covers:
- the player id (its install's sessions);
- every linked identity: each linked Discord / e-mail fingerprint is recorded against the banned install (`oauth2:ban:<kind>:<hash>`),
  so it cannot be linked to another player or move one while the ban lasts;
- the hardware hash: a ban blocks the install's recent device hashes by default. The moderation console sends
  `block_devices: false` to skip that (its checkbox), or names `devices` explicitly. Suspensions never block devices.

## E-mail sign-in (`services/online/lobby`: `SocialService.emailStart / emailRequest / emailVerify / emailPoll`)
1. The launcher: `POST /v1/auth/email/start {v, mode: link|restore|confirm, install, poll_hash, name?, device?, client?, recovery?}`
   (link / confirm with the session; restore with the new install) -> `{v, ticket, url, expires}`; it keeps a random poll secret
   (only its SHA-256 is sent) and opens `url` (`/email/<ticket>`) in the browser.
2. The page asks for the address, with Cloudflare Turnstile; `POST /v1/auth/email/request` checks Turnstile, the address, the CC0
   disposable-domain list (`data/disposable-domains.txt`, built into `src/disposable-domains.mjs`), and sends a one-time link
   (`/email/verify/<token>`, 15 minutes) through the mail function. The address is used for that send only.
3. The link opens a page with one button: `POST /v1/auth/email/verify` (a GET consumes nothing, so mail scanners cannot).
4. The launcher's `POST /v1/auth/email/poll {ticket, poll}` collects the result: the linked identities, a sign-in for a moved
   player, or a new recovery code.

## Storage budget (Cloudflare Free: 100,000 row writes a day)
Narrow budgets are counted in memory; admitted flows have daily caps. OAuth start / finish each allow link 600 and restore 400
in separate buckets. Unlink allows 200; e-mail sends share 300 across modes, also 3 per page and 3 an hour per
address. These are admission limits, not a guarantee that every refused flow writes nothing: a failure after admission can still
charge a durable budget or consume a transaction.

## Mail function (owner's choice: no static AWS keys)
`services/online/mailer/`: an AWS Lambda (Python, boto3) with a function URL, accepting only requests signed with `MAILER_KEY`
(HMAC-SHA256 over the time and the body, 5 minutes), sending one message through SES (us-east-2) from `no-reply@openbfme.com`; its
role may only call `ses:SendEmail` with that sender.

## Configuration (Worker secrets)
`OAUTH_DISCORD_CLIENT_ID` / `_SECRET`; `TURNSTILE_SITE_KEY` / `TURNSTILE_SECRET`, `MAILER_URL` / `MAILER_KEY` (all four, or e-mail
sign-in is off: 503 `email_unavailable`); `PRIVACY_URL` (default https://openbfme.com/privacy).

## Privacy page
`docs/PRIVACY-SIGNIN.md` (draft for the owner), linked from the launcher's sign-in screen only.
