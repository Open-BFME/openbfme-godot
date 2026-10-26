# Integrity checks on finished games (lane ANTICHEAT-1)

OpenBFME online games are deterministic lockstep: every client simulates every unit, so a map hack only reads its own memory and no server can hide
state from it. What we can do is check, after the game, what each player did against what that player could see: the relay keeps every command, and
the replay re-simulates exactly. These tools run those checks. Nothing here acts on a player: a result only asks a person to review.

## D1: orders on objects the player could not see

`engine/src/GameNetwork/OrdersIntoFog.h` (Palantír detector D1). While a replay is re-simulated, every frame it records which enemy objects each human
player could see (RotWK's own shroud rules: `ShroudManager::clientObjectStatus`, `hasSeen`, stealth), and it judges every object-targeted order
(attack, force attack, weapon, special power, combat drop, capture) on an enemy object:

| reason | meaning | confidence |
|---|---|---|
| `never_seen` | the player had never seen the target (e.g. a building clicked through the fog that the player could not have known existed) | 0.97 |
| `hidden_unit` | seen before, but not in the last 27 frames and hidden in fog now (a unit moving in fog) | 0.60 .. 0.95 with the time out of sight |
| `stealthed` | the target's ground was visible but the target was stealthed and undetected | 0.60: review only (disguises and detection timing not ruled out yet) |

A clean OpenBFME client cannot produce these orders: its click picks only what it draws (S-288), and RotWK hides enemy units in fog and enemy
buildings until first seen (RW 0x68EDD0). The 27-frame window covers the lockstep delay (run-ahead up to 25 frames) and the frame or two the client
shows behind. Allied vision and revealer powers need no special case: they look into the player's own cells. Remembered buildings (seen once, drawn as
a ghost in fog) are never flagged.

Bands, per player: events within 30 s of an encounter's first event are one encounter (not a sliding gap between adjacent events). `alert` = at least 3 encounters at confidence >= 0.9, the first and last at
least 2 minutes apart; `review` = any encounter; `none`; `abstained` = the game cannot be judged. The whole game abstains when the replay lost a
command, has no recorded hashes, starts from a save, or the re-simulation does not reproduce the players' own recorded hashes (`crc_parity`); a relay
log whose players' CRCs disagree (a desync) or that is incomplete abstains too (`fog_check.py run`). A replay without an end record (every relay
log's, S-3710) is judged up to its last recorded hash: every judged order lies before a state the players' own CRCs confirm.

Coverage limits: a map hacker who only looks, and clicks things once they are visible, leaves no D1 trace. D5 below checks input rate, not passive looking;
position orders (move, attack-move, spells at a location) are not judged.

Selections count too (owner, 2026-10-21): OpenBFME's client sends the selection of enemy objects over the network, and it can only pick what it
draws, so a selection (`MSG_CREATE_SELECTED_GROUP` and its forms) naming an enemy the player could not see is judged like an order on it.

## D2: view focus (client-reported, supporting only)

`engine/src/GameNetwork/ViewChecks.h`: the game reports each player's view twice a second (the camera's look-at point and the ground under the pointer,
`Network::submitView`) on the relay's log-only stream: the relay logs it, no other player receives it, it is no simulation input; the replay keeps it
(version 8 VIEW records). `hover_hidden`: the pointer rests for at least 3 s (15 frames) within 30 units of an enemy hidden from the player (a unit in
fog, a building never seen, a stealthed unit) that the player had not seen in the 10 s before. Band `review` at most: a modified client can report
anything or nothing (`views` counts what a player reported; D7 says whether its input was signed), and a pointer can rest anywhere by chance.

## D5: input rate ("macro detector", informational)

`engine/src/GameNetwork/CommandStreamChecks.h` `InputRateCheck`: every human player's commands counted over every window of 5 seconds of game time
(25 logic frames; deterministic, no wall clock). More than 100 commands in one window is an episode (overlapping windows are one episode: its peak,
the peak's frame, the most frequent command type). Macros and hotkeys are allowed: band `review` at most, never `alert`. `MSG_LOGIC_CRC` (the client's
own checksum) is not counted; a click usually sends two messages (the selection, then the order).

## D6: command integrity (modified clients)

`CommandIntegrityCheck`: commands an unmodified client never sends: `unknown_type` (outside RotWK's 1001 .. 1147), `malformed` (an argument of the
wrong type where the command's handler requires one), `foreign_object` (another player's object named as the issuer of a rally point, an auto
ability, a formation or an enter command; the logic already refuses them). Review-level until real games show the clean-client rate is zero.

D5 and D6 run in the same re-simulation as D1, under the same abstain rules, and write reports of the same shape (`detector` `D5-input-rate` /
`D6-command-integrity`; D5 events `{frame, time_s, command, count, start_frame, end_frame, encounter}`, players add `commands_total`, `peak_5s`,
`peak_frame`; D6 events `{frame, time_s, command, reason, target_id, argument, encounter}`, players add `commands_total`).

## D7: packet integrity (relay-observed)

The relay keeps, per slot, what it received and refused (INPUT records, relay log version 4, kind 7: RelayCore.h "u8 7 INPUT"); `openbfme_peer --relay-log
... --packet-check d7.json` reports it: `input` (`signed` / `plain` / `mixed`), the counts, band `review` when anything was refused for the slot
(datagrams speaking for another slot, malformed datagrams, and, with the relay's packet authentication, wrong tags, replays and unauthenticated data
from an authenticated slot). The authentication itself is the relay lane's (AUTH-1: every relay datagram tagged in both directions, keys derived from
the START's `session_key`, RelayProtocol.h sessionKey); it fills the INPUT record's verified / refused counts in this layout. Until it is deployed,
every slot's data counts as `plain`.

A game a previous release opened (its version-3 HELLO) has a version-2 relay log, which that release's clients read; its INPUT records go to a
sidecar next to it (`<log>.input`, RelayCore.h "INPUT sidecar"), which `--relay-log` reads when present. A player authenticating in such a game is
an anomaly (honest games never mix releases: a current participant can send its own ticket in a version-3 HELLO first to keep its INPUT records out
of the log): the report's top-level `anomaly` says so and the slot whose HELLO opened the game gets band `review`, with the same text in its
`anomaly`. The relay's uploader posts the sidecar after its log (`/v1/relay/logs/<id>/input`, stored by the lobby next to the log); a lobby
without that route keeps refusing it, the uploader keeps both files and retries, and a check of a log without its sidecar abstains as before.

## The automatic check

`integrity_check.py` is the program to run (or `openbfme-integrity.service` / `.timer`, every 5 minutes, on a host with the game files):

Linux command example, from the repository root with Python 3, a built
`openbfme_peer`, `ROTWK_INSTALL` / `BFME2_INSTALL`, and the named local game
records; set `GAME_ID` to that game's 16-hex ID. Evidence rendering also needs
ffmpeg (or pass `--no-render`):

```sh
python3 tools/integrity/integrity_check.py game --peer engine/build/openbfme_peer --relay-log G.obfmerlog --online-start G.start.json --game-id "$GAME_ID" --out-dir DIR
```

Watch-mode usage sketch (parentheses/brackets show alternatives, not shell syntax;
posting additionally needs an operator-provided intake URL and key file):

```text
python3 tools/integrity/integrity_check.py watch --once (--peer PEER | --peers DIR) --root ROOT (--incoming LOGDIR | --lobby URL --read-key-file K) [--url INTAKE --key-file K]
```

One game: `DIR/game.replay` (the whole game with the players' views, kept for the admins to watch in OpenBFME), `d1.json`, `d2.json`, `d5.json`,
`d6.json`, for a relay game `d7.json` (packet integrity), `summary.json` / `summary.txt`,
and for every player D1 flags `evidence/d1-slot<N>.png` / `.mp4`. `watch` does that for every finished game not checked yet (a relay log directory, or
the lobby's worker read routes) and posts the findings of the detectors the intake takes (`--detectors`, default D1).

A peer replays only the games of its own build (the profile identity's engine id hashes every engine source), so the timer keeps one checker per
release the players run: `--peers DIR` picks `DIR/<profile>/openbfme_peer` by the game START's `profile` (`openbfme_peer --print-profile` prints a
build's). A game whose profile has no checker is no failure: `state.json` keeps it under `unsupported`, it is checked once a checker for that profile is
installed, and after the lobby's 24 h roster window it moves to `expired` (never checked). A checker runs the detectors its release has (an older
release's peer has no D5 / D6: their reports are then absent); D1 is the minimum. The lobby's list is read from its `resume_cursor`.

### Running it

Usage sketches for the same Linux build and prerequisites; replace the uppercase
arguments, and do not type brackets. Posting is an operator action, not a local test:

```text
openbfme_peer --rotwk DIR --bfme2 DIR --replay GAME.replay --fog-check report.json [--game-id ID]
python3 tools/integrity/fog_check.py run --peer engine/build/openbfme_peer --relay-log GAME.obfmerlog --online-start START.json --game-id ID --out report.json
python3 tools/integrity/fog_check.py findings report.json [--post URL --key-file KEY]
```

`findings` prints one JSON line per player in the `review` / `alert` bands with a neutral one-line `summary` and the player's `evidence` (at most
50 events, `events_total` counts all); for an abstained game it prints one `abstained` record per player (no evidence, no summary: the lobby counts
abstentions and makes no flag); a report without a game id gives nothing. With `--post` it sends each, signed as the lobby's private routes are
(`X-OpenBFME-Admin: <unix-seconds>.<hex>`, hex = HMAC-SHA256(INTEGRITY_KEY, "POST /v1/internal/integrity/findings?\n<unix-seconds>\n" + body), the
lobby accepts +-300 s; 5xx retried with backoff, 4xx final: 409 finding_conflict, 404 unknown_game (rosters live 24 h after the game), 422), to the lobby's integrity intake (`POST /v1/internal/integrity/findings`, the Orthanc lobby's route; the lobby takes the
player's identity from its own roster, never from `name`).

Fixtures (`fixtures/`): `d1-evendim-alert.json` and `d1-evendim-clean.json` are openbfme_peer's reports of the two recorded Evendim games
(engine/tests/test_orders_into_fog_retail.cpp); `d1-derived-review.json` (the alert game cut to its first encounter), `d1-derived-abstained.json`
(the clean game as `fog_check.py run` marks a desynced relay log) and `d1-derived-crc-mismatch.json` (what `--fog-check` writes when a recorded hash
is not reproduced) are derived from them by the same rules.

### Report schema (v1)

```
{"v":1,"detector":"D1-orders-into-fog","detector_version":1,"game_id":"<16 hex>"|null,"status":"ok"|"abstained","abstain_reason":null|"...",
 "frames":N,"crc_parity":"match"|"mismatch"|"none",
 "settings":{"window_frames":27,"encounter_frames":150,"alert_confidence":0.9,"alert_encounters":3,"alert_span_frames":600},
 "players":[{"player_index":P,"slot":S,"name":"...","band":"none"|"review"|"alert"|"abstained","score":0..1,"encounters":K,"strong_encounters":K2,
   "orders_judged":J,"orders_on_remembered":R,"orders_stale":T,"orders_abstained":A,
   "events":[{"frame":F,"time_s":F/5,"command":"MSG_DO_ATTACK_OBJECT","target_id":ID,"target_template":"...","target_owner_index":O,
     "reason":"never_seen"|"hidden_unit"|"stealthed","last_visible_frame":null|F,"x":X,"y":Y,"confidence":C,"encounter":E}]}]}
```

`frame` is the replay frame the order ran on (seek the replay viewer there); `x`, `y` the target's position then.
