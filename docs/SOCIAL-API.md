# SOCIAL-1 lobby wire contract, v1

Authoritative S-A contract for S-B/C, S-D and S-E. Source: private ops
`design/social/SOCIAL-1.md` at `d25294e`, especially owner decisions §8.
This contract-first commit is an implementation target, **not a deployed API**.
Code lives only in private `Open-BFME/openbfme-dev`, branch `devin/social-a`.

Coordinator override: reports keep the frozen `submitPlayerReport` operation.
Recovery remains in scope. Friend chat is stored forever. The SOCIAL-2b extension
below supersedes the earlier OAuth deferral without changing S-A identity.
No automatic sanctions, appeals, name search, public player directory, or new
player-facing privacy prose is introduced by this backend work.

## Common wire rules

- HTTPS in production; local HTTP is test-only. Authenticated social routes require
  `Authorization: Bearer <lobby token>`; caller identity comes from it. Guest sign-in,
  OAuth restore start/finish and e-mail restore/page/poll routes use their separate
  proofs described below and in `IDENTITY.md`, not an existing lobby token.
- Session tokens (ONLINEMENU-1, PR #8 review finding 2) never show the install
  id, the credential that signs in as a player: they carry it sealed (`ix`) with
  a public tag (`ih`, lobby.mjs `installTag`) and the issue time `iat`. A token
  that shows `install` is from an older lobby and is refused (`401 signed_out`;
  clients sign in again). Identity changes (a recovery code, OAuth link and
  unlink) must name the session's install in the body (`install`); a session
  token alone gets `403 install_proof`.
- JSON objects, version `v:1` required on social POST/PUT bodies. DELETE and GET
  have no body. Unknown fields, duplicate query parameters, unknown query keys,
  malformed identifiers, unsupported versions and enum values are refused.
- Social success is HTTP 200 with a top-level `v:1`; no `data` wrapper. Empty
  mutations return `{v:1,ok:true}`. Existing non-social routes retain their
  status codes, including HTTP 201 for successful frozen report intake.
  JSON responses are `Cache-Control: no-store`.
- Errors retain the lobby envelope `{v:1,error:<code>,message:<safe text>}`.
  A 429 also has `retry_after` (integer seconds) and `Retry-After` header.
  Clients branch on `error`, not message text. Never echo tokens, recovery codes,
  install/device identity or request text in errors/logs.
- `pid`, request ID, invite ID and game ID are 16 lowercase hex characters.
  They are distinct namespaces. A roster `target` is **not** a social pid.
- Times are integer Unix seconds. Friend codes are ten random Crockford base32
  characters, displayed `XXXX-XXXX-XX`. Inputs allow either that uppercase form
  or ten uppercase characters; no other normalization or name lookup.
- Request body limit is the existing lobby 16 KiB cap, before parsing. Text
  limits count Unicode scalar values; reject unpaired surrogates, C0/DEL control
  characters, blank-only values and leading/trailing whitespace. No markup is
  interpreted. Notes are at most 200 characters; DM text at most 1000 characters
  and 4000 UTF-8 bytes. Missing optional notes become `""`; empty notes are legal.
- Lists below are bounded. Friends: 200. Incoming requests: 50 (oldest evicted).
  Outgoing requests: 200. Blocks: 1000, paged. Notices: last 50 per player.
  A failed reference/permission/cap check changes no relationship, room, invite,
  recovery binding or message; abuse-attempt counters may still advance.

## Identity and privacy

`POST /v1/auth/guest` retains WP-C's `{v:1,name,install,device?,client?}`;
`install` is 32 lowercase hex, `device` is 64 lowercase hex, client is
`{platform,version}`. The optional new `recover` field is described below.
Legacy sign-in remains compatible when `REQUIRE_INSTALL=0`, but social routes
then return `401 install_required`. New install-backed sign-ins add `pid` to
`account`: `{v:1,token,account:{id,name,kind:"guest",pid},notice?}`. `id` remains
the session's existing opaque roster identity; pid is stable across sign-ins.
No social response contains install/device identity. Ordinary reinstall keeps
the game-owned install file. Launcher uses the game's actual user directory and
`install_id.txt`, not a new launcher identity or the brief's obsolete filename.

`GET /v1/me` returns `{v:1,pid,code,nick,privacy,hidden}`. Privacy is exactly:

```json
{"invites":"friends_or_room","challenges":"friends_or_room","requests":"anyone","presence":"friends"}
```

`PUT /v1/me/privacy {v:1,privacy}` replaces all four settings and returns the
same shape as GET me. Allowed values: invites/challenges `friends_or_room` or
`friends`; requests `anyone` or `nobody`; presence `friends` or `nobody`.
Defaults above implement the owner override: friends **or** current-room peers,
not strangers everywhere. Friend-code requests remain possible with `nobody`.

`POST /v1/me/status {v:1,state,hidden?}` accepts `online|away|matching` only;
hidden is boolean, omitted means unchanged. Returns `{v:1,ok:true}`. Hidden is
persistent. Clients send activity state changes, not a separate heartbeat loop.
Inbox polls keep the session present but do not reset the 10-minute activity
idle clock; other authenticated actions do. Lobby membership overrides stated
state: `in_room`, `loading`, `playing`. Loading is the first 10 seconds after
authoritative START; playing follows until completion/leave. An active started
game counts as present even without polling. Otherwise presence expires after
150 seconds without authenticated traffic and becomes away after 600 seconds
without activity. Hidden, blocked, banned/suspended and presence=nobody players
appear simply offline, with no room/map or last-seen disclosure.

A `Player` is `{pid,nick,code}`. A `Presence` is
`{pid,state,game,joinable,since,map,mode}`: state is
`offline|online|in_room|matching|loading|playing|away`; game/map/mode are null
when unavailable, joinable boolean, since integer seconds (0 when offline or
hidden). Mode, when known, is `normal|uber`. Only accepted friends receive
presence. Private rooms are not advertised as joinable without an invite.

## Friends, blocks and recent players

- `GET /v1/friends` → `{v:1,friends:[{pid,nick,code,since,presence}]}`; sorted pid.
- `DELETE /v1/friends/{pid}` → `{v:1,ok:true}`; symmetric, idempotent. Cancels
  pending contact between the pair. Stored DM history is not deleted.
- `POST /v1/friends/requests {v:1,code}` **or** `{v:1,pid}` →
  `{v:1,request:Request}`. Exactly one selector. Pid must be a current-room peer
  or a participant in the caller's retained recent games. No arbitrary pid lookup.
  `Request={id,from:Player,to:Player,at}`. Both directions are visible in inbox.
  Existing friendship → `409 already_friends`; pending pair (either direction)
  → `409 already_requested`; no automatic reciprocal acceptance. Self →
  `400 self_target`. Unknown code, privacy refusal or block → `404 unavailable`.
- `POST /v1/friends/requests/{id}/accept {v:1}` → `{v:1,ok:true}`; recipient
  only, rechecks both friendship caps and block/enforcement before insertion.
- `POST /v1/friends/requests/{id}/decline {v:1}` → `{v:1,ok:true}`; recipient
  only. Missing/expired/wrong-recipient request → `404 unavailable`.
- `POST /v1/blocks {v:1,pid,note?}` → `{v:1,ok:true}`; target must be a known
  friend, request/invite correspondent, current-room/recent peer, or already
  blocked. Removes friendship and both directions of requests/invites atomically.
  Note is private to owner. No block notification is sent to target. Repeating
  updates the note. `DELETE /v1/blocks/{pid}` is idempotent, same success shape;
  does not restore friendship. `GET /v1/blocks?after=<pid>&limit=50` →
  `{v:1,blocks:[{pid,nick,code,note,at}],next:<pid|null>}`, ascending pid,
  limit 1..100, exclusive after (omit for first page).
- Blocking either direction prevents all new direct contact; host's block
  refuses the target's join/spectate. It does not kick a player already seated.
  Personalized game slot views add `pid` (null for AI/legacy users) and `blocked`
  boolean. Existing report `player_id` fields remain unchanged.
- `GET /v1/recent` → `{v:1,games:[{game,at,players:[Player with target]}]}`.
  Most recent 20 started games, descending at then game. `target` is that
  game's authoritative opaque roster ID for the frozen report route. Self is
  omitted, at most seven others per game. Entries do not grant access to other
  players' install IDs or presence. Games are recorded once at START.

## Invites and challenges; existing room integration

Room-code entry is provided by the existing lobby API: `POST /v1/games` accepts
`room_code` to create a code-private room, and
`POST /v1/rooms/<code>/join` joins it. Game-ID joins also accept `room_code`.
Legacy password-protected create and join remain supported. Room create response
stays `{v:1,id,game:<existing lobby game view>}`.

`POST /v1/invites {v:1,to,game,kind,mode?,note?}` → `{v:1,invite:Invite}`.
Kind is `invite|challenge`; challenge requires mode `normal|uber` and sender
must be host of an open private room protected by either a room code or a
password. To challenge, create that private room first and then send the
challenge. Normal invites omit mode (stored null). Mode is a display label;
profile/setup remain authoritative and must be compatible at acceptance.
Sender must occupy the supplied open game; there must be an open human slot.
Target must be friend or current-room peer under target's setting, with the
bounded room-peer challenge handoff below. Neither invite nor challenge payloads
contain a room code or password.

For a non-friend challenge selected in a public/current room, the client must
explicitly leave that room, create its new private room using either a room code
or password, then send the challenge with the **same session token**. A
successful explicit leave from an open room captures, server-side only, that
session's at-most-seven other seated social peers for 120 seconds. It replaces
any previous handoff. A new challenge room must have been created after that
leave; one successful challenge send consumes the corresponding peer entry.
Failed sends do not consume it. No client field, public grant or new route is
added. Kick, disconnect, START and recent-game history do not mint this
handoff. Restart/recovery/new sign-in loses it.

The delivered invitation freezes only this peer eligibility until its ordinary
expiry; the recipient may explicitly leave their old room before accepting.
Acceptance still rechecks privacy (friends-only rejects a non-friend), block,
enforcement, sender membership, room state, profile and capacity. The same rule
freezes direct same-room eligibility at send. Neither send nor accept leaves a
room implicitly. Friend-based invitations continue to require the friendship;
removal cancels them. A handoff is not reusable for another target, room, normal
invite, later sign-in, or after its 120-second expiry. Creating a later room
invalidates unused handoff entries rather than extending their lifetime.

`Invite={id,from:Player,to:Player,game,kind,mode,note,expires}`. Both invites
and challenges expire after 300 seconds, or immediately when the room
starts/closes, sender leaves, required friendship/privacy is removed or either
blocks. A host may challenge into its own open private room whether it uses a
room code or password. No slot is reserved by sending. Acceptance uses a
recipient-PID-bound, single-use server-only grant. It bypasses only the room's
code/password gate, never mode, profile, block, enforcement, capacity, START or
in-game checks. It is not a public parameter on the ordinary join route.

`POST /v1/invites/{id}/accept {v:1,profile,mode?,platform?}` →
`{v:1,id:<game id>,slot,game:<existing personalized game view>}`. The profile is
the accepting client's locally computed 32-lowercase-hex profile. Optional mode
and platform are supplied by the recipient; omitted mode means `normal`. The
server carries them into ordinary join validation and stores the recipient
platform on its slot. Acceptance mode is never inferred from the inviter or
invite. Wrong profile/mode, full room or other refusal leaves the invite
pending. Successful acceptance consumes it. Repeat accept → unavailable. It
never leaves another open room automatically (`in_game`).

Launcher cannot compute the mounted game profile and must never copy the host's
advertised profile. It hands **the same lobby token** and pending invite ID to
the game via `OPENBFME_LOBBY_TOKEN` and the S-B/C-owned
`--online-invite=<16hex>` seam. The game computes its local profile, calls accept,
then uses the returned game id in the ordinary join path. Ordinary join with
that token returns the reserved slot idempotently without requiring the password
again. Already-accepted game-owned invites can use `--online-join=<game id>`.
No new token is returned; no staging rejoin operation is needed. New sign-in
means a different roster account: do not replace the token between accept/join.

`POST /v1/invites/{id}/decline {v:1,busy?}` → `{v:1,ok:true}`, recipient only.
Missing/stale/wrong-recipient/blocked invites → `404 unavailable`. Decline emits
a sender notice `declined` or `busy`; expiry emits `expired`. The room remains.
Block cancellation is silent, not a distinguishable block event.

## Inbox and held-poll sequencing

`GET /v1/inbox?since=<opaque seq>&wait=25` →

```json
{"v":1,"seq":"<boot-id>:<revision>","reset":true,"presence":[],"requests":[],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":0,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25}
```

`stats` uses the existing `/v1/stats` object unchanged, as shown above.
`since` omitted means initial snapshot. It is opaque, at most 80 ASCII
characters, format 16 lowercase hex, colon, unsigned safe integer. `wait` is
an integer 0..25, default 0. Client sends the previous returned seq verbatim.

Every reply carries a **complete bounded replacement snapshot**, not append-only
arrays: presence of all current friends; all current incoming AND outgoing
requests/invites; up to 50 notices; up to 200 current friends' chat heads.
Absence removes prior entries. This intentionally avoids a lossy delta replay
protocol. `reset:true` on initial poll, restart/foreign epoch or a future seq;
otherwise false. Clients replace arrays even when reset=false. Seq changes
when that player's observable state changes; changes to unrelated users do not
wake it. Presence expiry/idle/loading transitions and invite expiry also change
the snapshot, at latest at held-poll timeout. Notices have stable random `id`,
`kind` (`declined|busy|expired|recovered`), `invite` (id or null), `at`; deduplicate
by id, never infer new events from an array's position. Chat heads are
`{pid,last_id,read_id}`; no DM text in the inbox.

If since matches current snapshot, wait holds until change/timeout. Inbox and
room holds share the existing 2-per-account and global hold caps; additional
inbox holds also cap at 2 per pid across sessions. When full, respond 200 with
the snapshot and `backoff:10`; client must delay. Disconnect/timeout/wake releases
exactly once. Deferred responses recheck authorization/enforcement before send.
Drop the signed-in 90-second stats poll. One sequential 25-second inbox hold
costs 144 timeout requests/player-hour (plus changes/sign-in); do not poll chat
or status on separate timers. Budget verification is recorded in ONLINE.md.

## Phase b: permanent 1:1 friend chat

Only accepted unblocked friends can send; chat-muted players cannot send DMs.
Read access to a past conversation stays with its two participants even after
unfriend/block. No third-party pid grants read access. Messages and idempotency
receipts have **no TTL** and are never purged by unfriend, block, recovery or
ordinary cleanup. Offline delivery uses persistent history plus inbox chat heads.

- `POST /v1/chat/{pid}/messages {v:1,client_id,text}` → `{v:1,message:Message}`.
  client_id is caller-generated 32 lowercase hex; scope is sender+conversation.
  Retry identical bytes with same client_id returns the original message, without
  another charge. Changed text → `409 idempotency_conflict`. Current send
  authorization is still required on replay. `Message={id,from,to,text,at,client_id}`;
  id is a positive safe integer, strictly increasing within the conversation.
- `GET /v1/chat/{pid}/messages?after=0&limit=50` →
  `{v:1,messages:[Message],next:<integer|null>,last_id,read_id}`. Ascending id;
  after is exclusive, default 0; limit 1..100, default 50. next is last returned
  id only when more rows remain. Empty history is legal for friends. Non-friend
  without a past conversation → `404 unavailable`. GET does not mark read.
- `POST /v1/chat/{pid}/read {v:1,through}` → `{v:1,read_id}`. through is a
  nonnegative safe integer no greater than the actual last_id. Monotonic update;
  cannot erase unread messages by claiming future ids. No read receipts sent
  to the peer. `GET /v1/chat?after=<pid>&limit=50` →
  `{v:1,threads:[{pid,nick,code,last_id,read_id}],next:<pid|null>}`. Ascending peer
  pid, limit 1..100; persistent thread index includes past friendships.

## Recovery

`POST /v1/me/recovery {v:1,install,recovery?}` → `{v:1,code:<16 uppercase Crockford chars>}`
(`install`: the session's install, `403 install_proof` otherwise). Once a
provider is linked (Sol r2), a new code needs the current one in `recovery`
(`403 recovery_required|bad_recovery`); a linked player without a code gets
`403 provider_confirm_required` and makes one with OAuth mode `confirm`.
Only a keyed hash is stored; new code rotates the old one. Show returned secret
once, never in URLs/logs. `POST /v1/auth/guest` with the normal new-install
fields plus `recover` atomically rebinds the existing pid, consumes the code,
and revokes all prior sessions for that pid (including after restart). The old
install cannot sign in again as that pid. New install already bound to another
pid → `409 install_in_use` (no merge/loss of either user's history). Invalid or
used code → `401 bad_recovery`; enforcement is checked for both identities,
so recovery cannot bypass a suspension/ban. New token carries the existing pid;
the player's new inbox gets a `recovered` notice. No Discord routes or
placeholder OAuth success are shipped by the base S-A implementation; OAuth is
defined separately in the SOCIAL-2b extension below.

## Reports and moderation bridge

Unchanged frozen route: `POST /v1/reports`, bearer + `Idempotency-Key`, body
`{v:1,game_id,target,reason}`. `target` is opaque authoritative roster player ID,
not pid/install/nickname. Reason is `cheating|harassment|griefing|other` optionally
followed by `: ` and short text (existing 1000-character total cap; social UI
should bound its optional note to 500). Response
`{v:1,report_id:"R-<24 lowercase hex>",received_at}`. Social Report actions need
a shared reportable game and roster target; disable with a named explanation
when no such context exists. No pid-only or DM-only report schema is added.
No new report-status API, client evidence upload or client-selected install ID.

Lobby resolves both identities and keeps private server evidence separately
from the frozen console source: up to the pair's last 50 held room chat lines
with game/nick/timestamp attribution. No client may submit evidence. Existing
WP-C authenticated `GET /v1/admin/mod/reports[/{id}]` is the console pull bridge
to its Reports queue/AdminFlags; configure distinct `MOD_KEY`, not an unsigned
outbound webhook. No new forward URL/credentials or live cloud calls required.
Evidence does not alter frozen list/detail projections. Reports retain their
90-day source lifecycle, 24-hour replay/duplicate windows and authoritative
game end +24h admission window. DM history remains permanent independently.

## Abuse, persistence and named refusals

Per-pid AND per-IP attempt limits (rolling windows, not full-window reset bursts):
friend requests 10/hour and 30/day; friend-code lookup 20/minute; invite/challenge
sends 6/minute, max 3 outgoing pending and 1 pending per sender/target pair.
Decline gives that pair 60-second cooldown; 3 declines within an hour suppress
it for 24 hours. Recovery generation/redemption 5/hour per IP (generation also
per pid). Chat sends 30/minute and 500/day per pid/IP, 10,000 admitted messages
per UTC day globally. These chat caps bound new permanent growth, not retention.
Existing report limits remain 5/hour, 20/day per install, 1000 admissions/day.
Quota accounting/insert/replay are atomic; malformed authenticated sends consume
attempt budget. Successful identical chat retries do not consume new admission.

Durable state: players/install and code indexes, recovery hashes/revocations,
friend edges, requests, blocks, recent games, chat/thread/read/idempotency rows
and abuse windows. Worker uses transactional storage on the existing SQLite DO;
Node uses durable local storage under LOBBY_DATA. No unbounded whole-chat reload
or rewrite. Store/config failure → `503 social_unavailable`, never ephemeral
success. Capacity failure → `507 social_store_full`, no deletion to make room.
Presence, invitations, notices and inbox epochs are memory-only: restart drops
them, retains durable state, re-announces presence only after authenticated
activity, and forces client reset. All production mutators use serialized
transactional admission. Stores are injected for deterministic offline tests.

Named errors: 400 `bad_json|bad_version|bad_field|bad_query|self_target|bad_reason`;
401 `signed_out|install_required|session_revoked|bad_recovery`; 403 existing
WP-C enforcement errors and `chat_muted`; 404 `unavailable|not_found`;
409 `already_friends|already_requested|friend_limit|request_limit|block_limit|
invite_limit|cooldown|idempotency_conflict|install_in_use|profile|full|started`;
429 `rate_limited`; 413 `too_big`; 503 `social_unavailable`; 507
`social_store_full`. Existing game/report errors (including `in_game`, `not_host`,
`report_expired`, `already_reported`) remain available unchanged. A block is
never named in a target-visible error. WP-C ban/suspend/shadow routing applies
to every send/read/accept and deferred poll; mute permits friendship and invite
accept, but refuses nonempty invite/challenge notes and DM sends.

## Required verification

Node route tests cover identity/recovery/restart, request/accept/decline/remove,
privacy and presence transitions, invites (friends and room peers), challenge
host/private checks, accept grant/profile/capacity/expiry, shared hold bounds,
blocks and hosted joins, unchanged reports + private evidence, chat replay,
offline delivery/read/pagination/permanent retention, per-IP/pid limits and
all malformed reference/enum/body/cursor refusals with unchanged application
state. Exercise the Worker adapter and durable restart, not only a memory mock.
Use fake AdminFlags pull and synthetic data only. Measure inbox request budget.
S-E owns three-client headless end-to-end QA on the CI hosts, not a developer machine.

## SOCIAL-2b: Discord account linking (Google dropped, e-mail added: docs/IDENTITY.md)

Coordinator-authorized extension, agreed with S-A on 2026-10-16. This replaces
SOCIAL-2's separate credential/PID model, not S-A's identity or social graph.
All PIDs remain **16 lowercase hex**. The game and launcher continue sharing
`install_id.txt`; no `identity_key.txt`, second player record, or account merge
is introduced. Unmodified PR #5 must not be layered over this implementation.
This is a contract, not evidence of deployment or live-provider qualification.

The existing four SOCIAL-2 route paths are retained; no fifth bootstrap route
is required. HTTPS, no-store responses, the 16 KiB request cap, strict fields
and named neutral errors apply. All four reject query parameters. Provider is
exactly `discord` (ONLINEMENU-1: Google dropped by the owner, 2026-10-20); provider subjects never come from the client.

### Start (`startOAuth`)

`POST /v1/auth/oauth/start` has three mutually exclusive shapes:

- **Link:** existing S-A bearer required; body
  `{v:1,provider,mode:"link",state,code_challenge,install,recovery?}`. `install`
  is the session's (`403 install_proof`). A provider already linked is never
  replaced (`409 oauth_provider_exists` at start). When another provider is
  already linked, `recovery` (the player's recovery code) is required:
  `403 recovery_required` (none sent, or the player has no code yet) or
  `403 bad_recovery`; 10 tries an hour per player.
- **Confirm** (ONLINEMENU-1, Sol r2): existing S-A bearer; body
  `{v:1,provider,mode:"confirm",state,code_challenge,install}` for a provider
  linked to the player (`404 oauth_not_linked`). Finish checks that the
  provider account is the one linked (`403 oauth_not_this_player`) and answers
  `{v:1,pid,providers,code}`: a new recovery code (the old one ends), no token.
- **Restore:** Authorization header must be absent; body
  `{v:1,provider,mode:"restore",state,code_challenge,name,install,device?,client?}`.
  Identity fields obey the normal guest rules. `recover` is not accepted.

`state` is 64 lowercase hex; challenge is 43 base64url characters (S256).
Return `{v:1,transaction,authorization_url,pid}`: transaction is 48 lowercase
hex, pid is the current PID for link and null for restore. Bootstrap does not
call guest sign-in, allocate a PID, bind an install, or disclose a linked PID.
The five-minute pending flow binds the identity fields immutably. Link also
binds account.id, PID, session generation and provider-link version.

The launcher binds `127.0.0.1:43827` before opening the system browser. The only
redirect is `http://127.0.0.1:43827/oauth/callback`; scopes are Discord `identify`
(Google dropped). Authorization destinations, scopes, redirect, state and
S256 challenge are checked before opening. Callback state/path/Host are exact;
duplicate query values and unexpected origins are refused. Secrets remain in
memory, never process arguments, URLs other than the protocol's required
authorization/callback parameters, preferences, diagnostics or logs.

### Finish (`finishOAuth`)

`POST /v1/auth/oauth/finish {v:1,transaction,state,code,code_verifier}`.
Link requires the same authenticated roster identity; restore forbids a bearer.
Verifier is 43..128 PKCE unreserved characters. Code is 1..2048 printable ASCII
characters without whitespace. Validate state/PKCE/expiry and consume the flow
atomically **before** provider exchange, including on provider failure.

Exchange against fixed provider endpoints without redirects, using bounded
responses and timeouts; verify the stable subject from fresh provider userinfo.
After exchange recheck the bound identity, generation, provider-link version,
provider mapping and old/new install/device enforcement before any mutation.

- Link attaches the subject uniquely to the existing PID. Another PID's link
  returns `409 oauth_conflict`; replacing a different account of the same
  provider returns `409 oauth_provider_exists`. No graph is moved or merged.
  Preserve the roster account.id and session generation.
- Restore requires an existing subject link (`404 oauth_not_linked` otherwise).
  Reject an install bound to a different PID with `409 install_in_use`, even
  if that binding appeared during the exchange. Rebind the same player using
  the shared recovery transaction helper. Validate player/install/code indexes;
  delete the old install index, keep the player's recovery code (ONLINEMENU-1:
  the owner can take the player back with it), record a `moved:<install>`
  notice for the install left (90 days; its next guest sign-in gets a one-time
  `notice` {id:"moved:<time>",text} naming the time and the way, and a new
  player; a recovery-code rebind records one too); increment
  the checked safe-integer session generation and bind the new identity.
  Preserve PID, friend code, privacy, friendships, blocks and permanent chat.
  Old sessions remain revoked across restart. After commit clear stale
  session-bound handoffs/presence/invites and reset/wake inbox with a recovered
  notice. No intermediate player or install mutation occurs on failure.

Return `{v:1,token,account:{id,name,kind:"guest",pid},providers:[...]}` with the
normal lobby token shape, plus the normal optional moderation notice. Link
retains account.id; restore creates a normal guest roster ID for the existing
PID. The launcher cancels old requests, adopts this token and reloads me,
friends and inbox. Never perform a second guest sign-in between this response
and room handoff. A 32-hex SOCIAL-2 PID is an invalid response, not an alias.

### Accounts / unlink (`getOAuthAccounts`, `unlinkOAuth`)

`GET /v1/auth/oauth/accounts` and
`POST /v1/auth/oauth/unlink {v:1,provider,install,recovery}` require an existing
S-A bearer; unlink also needs the session's install and the player's recovery
code (`403 install_proof|recovery_required|bad_recovery`).
Both return `{v:1,pid,providers:[...]}` with sorted provider names only. Unlink
removes just that provider binding; an absent link returns `404 oauth_not_linked`.
Link/unlink increment a separate checked provider-link version, invalidating
in-flight link/restore operations without replacing the roster/session identity.
Unlink does not delete friends, invalidate the local install, or erase chat.

### Storage, limits and failures

Provider-to-PID indexes, per-PID provider hashes/versions, pending flows and
expiry-indexed quotas use a distinct `oauth2:` namespace in S-A's
`social_records` and its **same** serialized synchronous transaction interface.
No network await or nested transaction occurs inside the shared rebind helper.
Only keyed/domain-separated subject hashes are persisted, never raw provider
subjects, provider tokens, authorization codes or PKCE verifiers. Provider
configuration remains Worker secrets; partial client-id/secret pairs fail.

Starts: 30/IP, 10/install, 20/PID when authenticated, 10,000/global per 10 min.
Finishes: 30/IP, 20/install, 40/PID when authenticated, 20,000/global per 10 min.
Unlinks: 20/IP, 10/install, 20/PID, 10,000/global per day. One pending flow per
install; replacements invalidate the old flow. Cleanup walks bounded expiry
pages, never an unbounded whole-store scan. A 429 supplies retry_after and
Retry-After. Invalid attempts may consume quota, never mutate player state.

Order (ONLINEMENU-1, PR #8 review finding 1: one address could use up a global
budget and turn linking off for everyone): every narrow scope (address,
install, PID) is checked first and nothing is written when one refuses; a
global budget is charged only by a request that passed them all and is well
formed (the route charges the address alone before it reads the body). "IP" is
an IPv4 address or an IPv6 /64; an IPv6 /48 also has a budget of 4 /64s. Starts
and finishes have separate global budgets for link and restore, so a restore
flood (no sign-in needed) cannot use up linking's budget. Residual risk: many
distinct addresses together can still reach a global cap; that cap exists to
bound provider calls and Free-plan storage writes.

Storage (Sol r2: the Free plan's 100,000 row writes a day are shared with every
lobby operation): every narrow budget (address, /48, install, player; Sol r3)
is kept in memory; only requests admitted by the global budgets write. A refusal
after admission can still charge a durable budget or consume a transaction.
Each global bucket has a daily cap (start and finish each: link 600, restore 400;
unlink 200 per UTC day), which keeps
OAuth near 25,000 row writes a day at worst.

Existing S-A storage/enforcement/session errors remain authoritative. Additional
errors: `unknown_provider`, `provider_unavailable`, `oauth_expired`,
`oauth_state`, `oauth_verifier`, `oauth_stale`, `oauth_conflict`,
`oauth_provider_exists`, `oauth_not_linked`, `oauth_provider_unavailable`.
Tests must cover mock-provider link/restore through real Node/Worker storage,
restart preservation, unchanged PID/code/friends/chat, old-session revocation,
conflicting installs, asynchronous mutation races and failures without partial
identity changes. Live Discord qualification still requires the owner's app.
