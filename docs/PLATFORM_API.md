# OpenBFME online platform API v1: the game's side

Lane ONLINEHUB-1 (2026-10-14). The coordinator's contract (briefs: platform-api.md) decides the architecture: the lobby stays on Cloudflare
(play.openbfme.com: rooms, staging, chat, relay), the persistent services (Quick Match, tournaments, statistics, game results) run on AWS behind
`https://api.openbfme.com/v1`, built in a private repository. This page is what the game sends and accepts, field by field, so the service and its
contract tests can be checked against the client. Where the contract left a shape open, the choice made here is marked **(client decision)**.

The game's code: `engine/src/GameNetwork/PlatformApiClient.{h,cpp}` (the client: no I/O, every answer checked), `engine/src/GameClient/GameRecords.{h,cpp}`
(the local record of each finished game and the results body), `engine/src/GameClient/GUI/AptScreens/AptOnlineHub.{h,cpp}` and `AptStats.{h,cpp}` (the
pages), `godot/scripts/online_http.gd` (the transport, kind `platform`). The tests run against the backend's OWN service and
HTTP fake, copied into `tools/platform_fake/` (origin and commit in its `SOURCE.md`; `fake_server.py` adds a `/__test/` surface: request log, clock,
injected failures), so the client and the real backend cannot drift: `python3 -m pytest tools/platform_fake`, and the C++ screen tests start it.
Run that command from the repository root on Linux (or another POSIX test host), with Python 3, pytest and a loopback port available.

## General rules

- Base URL: one value, `--platform-url=URL`, else the `OPENBFME_PLATFORM` environment variable, else Options.ini `OpenBFMEPlatformUrl`, else
  `https://api.openbfme.com/v1`. Request paths below are relative to it (`/results` is `https://api.openbfme.com/v1/results`).
- JSON both ways, `"v": 1` in every body. The client refuses an answer that is not a JSON object with `v` = 1, an answer larger than 128 KiB, a field
  outside its type, bound or closed set (named in the error), and keeps its previous state when it refuses. Redirects are never followed.
- Every write (`POST` / `DELETE` bodies) carries `"client":{"platform":"<windows|linux|macos|...>","version":"<game version>"}` (the backend answers 400
  without it).
- Success is 200, or 201 for a created ticket / registration. A refusal: any other status with `{ "v": 1, "error": { "code": "<code>", "field": "<name>" } }`
  (the backend's envelope: code `[A-Za-z0-9_-]{1,64}`; `field` optional; no message, so the game shows its own words for the codes it knows and the
  code itself for any other). A 429 / 503 may carry `Retry-After`.
- Identity: `install` = 32 lowercase hex digits (128 random bits, made once per installation, kept in the game's user folder:
  `user://platform_install_id.txt`), and `nick` (the online sign-in name, 1 .. 24 bytes UTF-8, no control characters). No accounts, no tokens.
- Ticket, Quick Match match and tournament ids: 32 lowercase hex digits. Tournament match ids: `<32-hex tournament id>.<24 lowercase hex>`.
  Result game ids: 1 .. 128 ASCII letters, digits, `_`, `-` or `.`. A room code: `[A-Za-z0-9_-]{6,32}` (the lobby's `ROOM_CODE`).
- Nothing blocks a game: the service unreachable means "Online services unavailable" on the pages, and play goes on.

## Quick Match

`POST /qm/tickets`
```json
{"v":1,"install":"<32 hex>","nick":"Elrond","client":{"platform":"linux","version":"<game version>"},"mode":"faithful","size":"1v1","factions":["FactionMen"]}
```
`mode`: `faithful` (Normal) or `uber` (the player's Uber performance mode option). `size`: `1v1` / `2v2`. `factions`: PlayerTemplate names, or
`["random"]` (1 .. 32 entries). Answer: 201 `{"v":1,"ticket":"<id>","poll_after_s":2}` (`poll_after_s` 1 .. 60; one outside it, or not a whole number, ends the search as a protocol error).

`GET /qm/tickets/<ticket>` every `poll_after_s` (an answer may carry a new `poll_after_s`; without one the client keeps the last one it was given):
```json
{"v":1,"state":"matched","poll_after_s":2,"match":{"id":"<id>","room_code":"QM-1a2b3c4d5e6f","host_install":"<32 hex>","mode":"faithful",
 "players":[{"nick":"Elrond","team":0},{"nick":"Gothmog","team":1}]}}
```
`state`: `searching` / `matched` / `expired` / `cancelled`. A matched ticket's match must have the search's mode, 2 players for `1v1` and 4 for `2v2`,
teams 0 .. 7, and this player among them (the matching `host_install` identifies the first player; a non-host must have exactly one matching nick
among the remaining players; a wire `you` flag is not used) - anything else ends the search with
the reason shown. `DELETE /qm/tickets/<ticket>` cancels (also sent when a cancelled search's ticket arrives late, and when the client's own search time
runs out: 10 minutes). A ticket answered after the search was cancelled or restarted belongs to the old search: it is cancelled and never adopted.

The client ends a search with a reason when: the ticket is refused, three polls in a row fail (transport or 5xx / 408 / 429), the service says expired
or cancelled, or no match in 10 minutes.

After `matched`: the install named by `host_install` creates a **private lobby room** named by `room_code` (lobby `POST /v1/games` with `room_code`; the
room is not listed and has no password) on one of the official multiplayer maps for exactly that many players, picked by the match id (FNV-1a of the id
modulo the maps in the skirmish list's order: every player computes the same one); the others join it with the lobby's `POST /v1/rooms/<code>/join`
(retried every 2 s while the host has not made it). Each player takes the match's team and its own army and colour, says ready; the host starts once
every player of the match is in and ready. The room must start within 90 seconds of the match, else everyone leaves it and is told. **(client
decision: the platform's match carries no map, so the map is chosen this way.)**

## Tournaments

`GET /tournaments`:
```json
{"v":1,"tournaments":[{"id":"0123456789abcdef0123456789abcdef","name":"Spring Cup","mode":"faithful","size":"1v1","start":1790000000,"max_players":8,"registered":3,
 "state":"upcoming"}]}
```
At most 100 per page; ids unique; `state` `upcoming` / `running` / `finished` / `cancelled`; `registered` <= `max_players` (2 .. 128);
`name` at most 512 UTF-8 bytes.

`GET /tournaments` is paged: `next_cursor` (when present) is asked with `?cursor=`; the client reads at most 5 pages (500 tournaments) and says when there are more.

`GET /tournaments/<id>?install=<install>` (the backend's flat shape: the tournament's fields beside `rules`, `reason`, `rounds`):
```json
{"v":1,"id":"0123456789abcdef0123456789abcdef","name":"Spring Cup","mode":"faithful","size":"1v1","start":1790000000,"max_players":8,"registered":3,"state":"running",
 "rules":"Best of one.\nNo rushing.","reason":null,"rounds":[["0123456789abcdef0123456789abcdef.0123456789abcdef01234567"]],
 "matches":[{"id":"0123456789abcdef0123456789abcdef.0123456789abcdef01234567","round":0,"state":"open","players":["Elrond","Gil"],"winner":null,
 "room_code":"T-example-r1m1","opened_at":1790000100}]}
```
`rules` at most 16384 UTF-8 bytes; at most 8 rounds and 255 matches total. `rounds` lists match ids; the separate `matches` array lists their
records with zero-based `round` and exactly two nicknames or null placeholders. Ids are unique and must belong to this tournament and round.
Match `state`: `waiting` / `open` / `ready` / `bye` / `finished` / `admin_resolved` / `no_show` / `disputed`; check-in uses `open` / `ready` with
`opened_at`. The public detail names no
registrants, so the game cannot know from it that you are registered: it remembers its own register / withdraw answers for the session (S-4873); after a restart Withdraw is offered on its own (the service's `DELETE .../register` of a place never held answers `registered: false`). A request that cannot be queued (64 waiting) refuses a Quick Match search by name, and a ticket request not answered within 60 s fails the search. "Your next match"
is the first `open` / `ready` match with both players and a room code for this install, in the service's match order. Its room is made as a Quick Match's: **the first
player listed hosts (client decision)**.

v1 is **1v1 only**: a tournament whose `size` is not `1v1`, or whose `mode` is not the player's current mode setting, is not joined (the player is
told why, nothing is sent). A match is entered from the room screen only after **check-in**: `POST /tournaments/<id>/matches/<match>/checkin`
(`{"v":1,"install":...,"client":{...}}`; answer `{"v":1,"state":"open"|"ready"}`; `checkin_closed` after the 15 minute window); the
game sends it when the player enters the match's room and retries twice, then tells the player.

`POST /tournaments/<id>/register` `{"v":1,"install":"...","nick":"...","client":{...}}` and `DELETE /tournaments/<id>/register`
`{"v":1,"install":"...","client":{...}}`: answer `{"v":1,"registered":true}` for registration (201 when new), `registered:false` for withdrawal.
The client checks that boolean; refusals use local text for known error codes, not a backend message. The game then reads the list again.

A tournament match is reported through `/results` with `game.id` = the match id (below). Admin routes are not in the game.

## Statistics

`GET /stats/global?mode=<''|faithful|uber>&period=<7d|30d|all>`:
```json
{"v":1,"as_of":1790000000,"games":40,"factions":[{"faction":"FactionMen","picks":30,"pick_rate":0.375,"wins":18,"win_rate":0.6}],
 "matchups":[{"a":"FactionMen","b":"FactionMordor","games":20,"a_wins":12}],"maps":[{"map":"maps/map mp fords of isen/map mp fords of isen.map","games":25}],
 "avg_duration_s":1250.5,"vs_ai":{"games":7,"factions":[],"matchups":[],"maps":[],"avg_duration_s":900}}
```
Counts whole numbers 0 .. 10^9, rates 0 .. 1, `wins` <= `picks`, `a_wins` <= `games`, `avg_duration_s` 0 .. 604800; at most 32 factions, 32
matchups and 32 maps per bucket. Labels may also be the service's display labels (at most 512 UTF-8 bytes); entries can mark `overflow:true`.

`GET /stats/me?install=` is not called by the game: "Your stats" are computed from the games this installation recorded itself (below), which also
counts skirmish games and works offline.

## Results

After the score screen of every LAN or online game (never a skirmish, never a replay, never as an observer), in the background, unless the player turned
off Options > Online > "Share my game results for statistics" (on by default):

`POST /results`
```json
{"v":1,"install":"<32 hex>","nick":"Elrond","client":{"platform":"linux","version":"<game version>"},"game":{"id":"<id>","mode":"faithful","map":"maps/...","players":[{"slot":0,"nick":"Elrond",
 "faction":"FactionMen","team":0,"human":true},{"slot":1,"nick":"Gothmog","faction":"FactionMordor","team":1,"human":true}],"start":1790000000,
 "end":1790000600,"duration_s":590,"winner_team":0},"me":{"slot":0}}
```
- `game.id`: the platform's match id for a Quick Match or tournament game, else the lobby's game id; a LAN game's is `lan-` and 32 hex digits of the
  SHA-256 of its start message (seed, map, slot names), so every peer of the game reports the same id **(client decision)**.
- `team`: the game's team; a player without a team gets the lowest unused nonnegative team number, in player order. `winner_team`: the team of the players the score screen calls
  victorious, `null` when there are none or they are not one team (a draw, everyone left). `duration_s`: the game's own time (logic frames / 5).
- `faction`: the PlayerTemplate the player actually played (a random choice resolved).
- `client` (platform, version) rides along as on every write. A game with fewer than two players is never sent, and neither is an online
  game with a single human (SOLO-1: a custom game may start solo; its result is nobody's statistics).
- Sent once per game, and not at all once the player turns the option off: results still waiting in the queue and retries are dropped at that moment.
  When the service cannot be reached (or answers 5xx / 408 / 429) it is sent once more 60 s later, then dropped and logged. A
  refusal (4xx) is not retried. The serializer includes `replay_hash` when the record has one (64 hex digits); the game's current record producer
  does not populate it.

## What the game sends (for PRIVACY.md)

PRIVACY.md lives on the public `main` branch only (docs/RELEASE.md: the sync keeps main's copy), so this lane cannot change it. The text to add there:

> **OpenBFME game: online statistics, Quick Match and tournaments.** When you use the online menu's TOURNAMENT or STATS pages, or after a LAN or online
> game while "Share my game results for statistics" is on (Options > Online; on by default), the game sends the OpenBFME online services
> (api.openbfme.com, Amazon Web Services) a random installation id made once on your computer (not tied to you or your hardware) and your online
> nickname, the game's version and your operating system's name, and: for Quick Match your mode, game size and army choice; for tournaments your registration; for results the game's summary (its id, mode,
> map, start and end time and length, each player's slot, nickname, army, team and whether a computer played it, and the winning team). Nothing else is
> sent: no IP address is stored with this data (rate limits use a short-lived hashed address), no hardware or system details. Turning the option off
> stops the results; the statistics of your own games are then kept only on your computer (user://game_records.jsonl). Statistics shown to everyone are
> totals only (no names).
