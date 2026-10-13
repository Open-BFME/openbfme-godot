# Online play

Online play is a **test feature**. OpenBFME plays skirmish, the campaign
and **LAN** games (the game's Multiplayer menu); the main menu's **Online**
opens RotWK's own online screens on the OpenBFME lobby (phase O2, below), and
the command line and `openbfme://join` links reach the same lobby (phase O1).

The plan (the owner's direction of 2026-10-08, docs/ROADMAP.md, "Online
multiplayer direction" and M6):

- **An OpenBFME relay service** carries online games: a lobby and server
  browser in place of the old GameSpy service, with accounts.
- **Players never see each other's addresses**: the relay passes every turn
  on, so strict routers work too.
- **The server keeps a log of each game**: replays, reconnecting after a drop
  by catching up, spectators.
- **An optional verifier** runs the same deterministic simulation on the
  server, for fast rejoins, multiplayer saves, desync and tamper detection and
  trusted results.

The client side of the protocol lives in this repository; the hosted service
is run separately. Phases: a relay with lobby and game log first (after the
closed playtest), then the verifier, then the browser, accounts and
spectators. Playing against the original game's 2.01 clients is a separate,
best-effort goal with its own checks (docs/PLAN.md, "Oracles and gates").

The rest of this page is the engineering study behind that plan (lane
ONLINE-1): hosting options, costs and the measured bandwidth.

## Online multiplayer: engineering options (ONLINE-1)

Design and measurement only (lane ONLINE-1, 2026-10-10). Nothing here has been
deployed, bought or signed up for. Prices were read on 2026-10-10 from the
pages cited in [Sources](#sources); they change, so re-check them before
spending money. The direction this works out is ROADMAP "Online multiplayer
direction": an OpenBFME relay service (lobby, server browser, turn relay,
server-side game log). The client protocol is public; the hosted service stays
private.

**Recommendation in one paragraph.** Put the lobby, server browser and sign-in
on a Cloudflare Worker with one Durable Object per region (Workers Paid, $5 a
month). Store replays in Cloudflare R2, which has no egress fees. Run the UDP
turn relay as a small C++ process on two cheap VPSs with bandwidth included,
one in US East and one in Western Europe (OVHcloud VPS, or Hetzner in the EU).
At the measured load (1,000 players at peak, 300 in game on average) that
comes to about **$20-30 a month**, and it stays under $45 at three times that
load. The fully AWS option only fits the budget on Lightsail bundles, and its
$0.09/GB overage breaks the budget as soon as the load grows. Steam join
through Spacewar (app 480) is not a legitimate route for a public release. Use
an `openbfme://join/<lobby>` link and Discord invites instead.

## Phase O1: what is built (ONLINE-2)

Lane ONLINE-2 (2026-10-11) built phase O1 (section 6). Online play works from
the command line and from `openbfme://join/<id>` links; the online screens
(phase O2) are not built. **It is a test feature.** By the owner's decision
of 2026-10-11, the service source is public, in `services/online/` (the
first paragraph of this document said it would stay private). Its secrets are
files read at deploy time ([DEPLOY.md](../services/online/DEPLOY.md)).

| Piece | Where | What it does |
|---|---|---|
| Relay envelope and tickets | `engine/src/GameNetwork/RelayProtocol.{h,cpp}`, `Common/Sha256` | 4-byte envelope (`BR`, version 1, kind: HELLO / WELCOME / DATA / DATA_FROM / NOT_BOUND / REFUSED / BYE), plus the target or source slot on data: 5 bytes per datagram. A 32-byte ticket: game id, slot, slot mask, expiry, and 16 bytes of HMAC-SHA256 under a key ring that the lobby and the relays share. |
| Transport relay mode | `GameNetwork/Transport.{h,cpp}` | Every peer's address is the relay's. Packets go out as DATA plus the target slot and come in as DATA_FROM plus the source slot. HELLO (the ticket) is sent until WELCOME, and again after NOT_BOUND (NAT rebinding, relay restart). The sender check becomes a slot check: the packet's sender byte must equal the source slot the relay stamped. Acks, resends and fragments stay end to end. |
| The relay | `services/online/relay` (`RelayCore`, `relay_main.cpp`) | One thread, `recvmmsg` / `sendmmsg` batches of 64. A HELLO with a valid ticket binds an address to (game, slot). DATA is forwarded in place, after checking the sender byte against the bound slot, the target and a per-slot token bucket (32 KB/s, 256 KB burst). A game closes on every BYE or 60 s of silence. Then the relay writes the END record and keeps a tombstone that refuses late HELLOs. A JSON status file serves as the health check. |
| Command log → replay | relay: the log; `GameNetwork/RelayLog.{h,cpp}`, `openbfme_peer --relay-log` | The relay follows every (sender, target) transport stream: in sequence order, fragments joined, duplicates dropped. It logs each sender's frame commands **once**, counting per frame across the streams. The converter assembles the frames as `Network::relayCommands` does and writes a replay whose hash records are the players' MSG_LOGIC_CRCs. `openbfme_peer --replay` then checks CRC parity. Limits: S-3710. |
| The START from the service | `GameNetwork/OnlineStart.{h,cpp}`, `NetGameSession` | JSON (`"kind": "openbfme-start"`, version 1): game id, slot, relay address, ticket, run-ahead, CRC interval, the profile identity's MD5, and the setup (map, seed, cash, rules, slots). The service picks the seed, the relay and the timing. The client checks every field, its profile, and that the ticket matches its slot and the human slots. It then plays the game as a LAN START. |
| The lobby | `services/online/lobby` (`lobby.mjs`; `worker.mjs` for Cloudflare, `server.mjs` self-hosted) | JSON over HTTP, `"v": 1`: guest sign-in (a signed token; Discord sign-in was dropped in round 2 until it has a way back into the game); list / create / join (the profile is compared) / leave / slot / setup / ready / chat / kick / start; polling with `?since=`; relay log upload (HMAC-signed, checked against the log's own game id) and `/v1/replays/<id>/{log,start}` (for that game's players only); an invite page `/j/<id>`. Rate limits are per account and per address. Games are kept in Durable Object storage (Cloudflare) or JSON files (self-hosted). |
| Game client | `GodotDevice/GodotGameWorld` (`net_online`, `net_profile_digest`), `godot/scripts/online_lobby.gd`, `game.gd` | `--online-join=ID` (the debug "join by id"), `--online-host`, `--online-lobby=URL` (or `OPENBFME_LOBBY`), `--online-name`. A bare `openbfme://join/ID` argument also works. `net_status` reports the relay link. |
| Launcher | `launcher/scripts/core/url_handler.gd`, `main.gd` | `openbfme://join/<16 hex>` starts the version Play starts, with `--online-join` / `--online-lobby`; nothing else is accepted. `--lobby=URL` remembers the lobby. `--register-url-handler`: HKCU on Windows (no UAC), an `x-scheme-handler/openbfme` desktop entry plus `xdg-mime` on Linux / the Deck. |
| Headless tools | `tools/net/online_client.py`, `tools/net/online2_soak.sh`, `openbfme_peer --online-start / --print-profile` | host / join through a lobby, or signed fixture STARTs; the relay soak (impaired links, log → replay → CRC parity) |

Tests: `engine/tests/test_online_relay.cpp` (SHA-256 / HMAC vectors, the
pinned ticket, the START, the relay core, three real Transports through the
relay with 20 % loss, log → frames, the relay process), `services/online/lobby/test`
(node --test, the same ticket pin), `tools/net/test_online_client.py`, the
launcher's unit test (links, registration entries).

### Security review, round 2 (Sol r1)

Round 1 was rejected on security. Each item is fixed with a test that replays
the reviewer's attack:

- **Player takeover.** `/v1/replays/<id>/start` handed anyone a live ticket,
  and a ticket shown from another address moved that player's relay slot.
  Now:
  - the published record is an `openbfme-replay-start`: no ticket, slot or
    relay, and the game refuses it as a START;
  - the relay gives each slot a random 16-byte session token in WELCOME
    (envelope version 2). Once a slot is bound, only a HELLO that carries
    this token moves it (a NAT rebinding); the ticket alone gets
    `REFUSED: slot taken`.
- **Relay DoS and log integrity:**
  - the budget is an integer token bucket (milli-bytes) that a rebind no
    longer refills;
  - the bytes held for the log (early records plus the command being joined
    from fragments) are capped per stream (1 MiB), per sender (4 MiB), per
    game (16 MiB) and per relay (256 MiB);
  - a packet the receiving Transport would refuse whole is neither logged nor
    forwarded: size over 1,200, CRC, layout, trailing bytes, a record beyond
    the window, an ack beyond what the target sent.
- **Windows URL argument injection.** The launcher takes a link only as
  exactly `-- --open-url <link>`, and checks this before anything else reads
  the command line. Any other argument refuses the whole start. The link must
  match `openbfme://join/<16 hex>` exactly.
- **Redirects.** The game's lobby client follows no redirect (Godot would
  send the bearer token to the new origin), and the lobby URL must be a plain
  `http(s)://host[:port][/path]`. The privacy guard checks both.
- **The lobby:**
  - START takes no join, slot or setup change while it signs;
  - the Durable Object queues its requests;
  - request bodies are limited by the bytes actually received (also when
    chunked);
  - Discord sign-in is removed; guest sign-in stays.

**Round 3 (Sol r2):**
- A discarded or finished partial command releases its allocation, and the
  caps count allocated bytes (capacity, plus a per-record overhead for early
  records). Sol's sequence had left 3,600 bytes allocated against a
  2,000-byte cap.
- The Worker reads a body before the request joins its queue, against the
  byte limit (413) and a read timeout (408). An endless body no longer holds
  the health check. The self-hosted server has the same timeout.
- A relay rate of 0 means no refill.

**Round 2 on the home lab** (lobby and relay redeployed on the VM). The same
3-player, 1,200-frame game was played: a headless host on the VM, the Godot
client and a headless peer on the Deck.

- All three ended on 0xD9D9EEAE with 11 CRC checks passed and 0 desyncs.
- The relay log went through the signed upload. The replay from it, made
  with the published replay START, matched all 12 CRC frames and ended on
  0xD9D9EEAE.
- During the game, a HELLO from another address with a real player's ticket
  was refused ("slot in use"), with or without a guessed token. The
  published record had no ticket, slot or relay.

### Measured (2026-10-11)

**Relay soak** (`tools/net/online2_soak.sh`, a local relay on a CI VM). Every
peer's outgoing datagrams had latency 80 ± 30 ms, 3 % loss, 1 % duplication
and 1 % reordering, on top of the relay hop. Three games of 3,000 frames each:
2 players; 4 players; 4 players + 2 AI on a second map. Results:

- Every game ran every frame on every peer, with 0 desyncs and one final hash.
- Every peer bound to the relay with one HELLO; 0 slot mismatches.
- Every relay log was complete with no gaps. The replay made from it matched
  the players' CRCs at all 30 CRC frames of each game.
- In the 2-player game the replay's final state equals the players' final
  hash (0xED492104).
- Stalls: p50 51-83 ms, p95 197-287 ms; one stall over 1 s in each 4-player
  game.
- Upload per peer: 225 KB per 2-player game and 680 KB per 4-player game,
  over 3,000 frames. That is 75 and 227 bytes a frame, in line with
  section 2's 0.57 KB/s per directed link.

**Home lab, end to end.** The self-hosted lobby and relay
(`services/online/deploy/selfhost.sh`) ran on a home-lab VM; clients were on
the LAN, with the Deck on Wi-Fi.

- **3 players, 1,200 frames, "fall back 4p" with one AI:**
  - the lobby game was created by a headless host peer on the VM;
  - the Godot client on the Deck joined with `--online-join`;
  - a headless peer on the Deck joined too.
  - Result: all three ended on 0x2FE1D65A, 11 CRC checks passed, 0 desyncs.
  - The relay's log went to the lobby through the signed upload. Fetched back
    from `/v1/replays/<id>/log`, it made a replay that matched all 12 CRC
    frames and ended on 0x2FE1D65A.
- **2 players, 600 frames:** the Godot client was started with a bare
  `openbfme://join/<id>` argument, as the launcher passes it on. Both players
  ended on 0xCD2A45CD. The round trip through the relay was 38 ms.

**Not tested:**
- JonathanPC took no part (its CI was paused for the owner's use), so no
  Windows client and no client behind another NAT;
- the Cloudflare deployment. The account exists now, on Free plans only.
  CFDEPLOY-1 made the lobby fit the Workers Free plan: relay logs go in
  Durable Object SQLite, START is refused while there is no relay, and
  `deploy/cloudflare_api.py` does the deploy. It is not live yet, because the
  owner's token launcher only works in their own Windows session. See
  services/online/DEPLOY.md, "The live setup";
- the launcher's registration on a real desktop (its entries are unit tested).

### What the owner needs to provide for a public deployment

- **Cloudflare:** an account on Workers Paid ($5 a month), and an API token
  with Workers Scripts Edit and Workers R2 Storage Edit (plus Workers Routes
  Edit for a custom domain). Also the account id. R2 must be enabled on the
  account; the replay volume fits the free 10 GB.
- **One VPS for the relay** (a second one later for the other region), Debian
  or Ubuntu, with a public IPv4 address and UDP 7350 open in the provider's
  firewall. The recommendation is OVHcloud VPS-1 at about $5-8 a month, with
  traffic included. The VPS needs ssh access for a sudo user.
- **A domain** (or a subdomain, such as `lobby.<domain>`) on Cloudflare for
  the lobby and the invite pages. Without one, the `*.workers.dev` URL works,
  but invite links look less trustworthy.
- **Decisions:** the lobby URL that release builds of the launcher should use
  by default (today it is set with `--lobby=`), and when online play stops
  being a test feature.

Monthly cost of this setup: about **$10-15** to start: $5 for Workers Paid
plus one relay VPS at $5-8 (the domain is about $10 a year). It rises to the
$20-30 of section 2 with a second region's relay at 1,000 players at peak.
Hosting both on the home lab costs nothing extra. It would need the router
to forward UDP 7350 and TCP 443 (behind a TLS proxy), which this lane did
not do.


## Phase O2: what is built (ONLINE-4)

Lane ONLINE-4 (2026-10-12) built section 5's flow, steps 1 to 7, on the
retail movies. **The whole Online menu is a test feature** (docs/PLAYING.md).

| Piece | Where | What it does |
|---|---|---|
| The lobby client | `engine/src/GameNetwork/OnlineLobbyClient.{h,cpp}` | The online counterpart of `LANAPI`: guest sign-in, the game list, the message of the day, create (name, password, the setup), join (the profile identity, the password), the player's own slot, the host's slots and setup, ready, chat, kick, start, leave; it polls the staging room once a second. It does no I/O: it queues HTTP requests (one in flight, in order) and reads the answers. Every answer is checked (version 1, the fields, the faction and colour ranges); a refusal or a transport error is an event with the lobby's code and message. |
| The screens | `engine/src/GameClient/GUI/AptScreens/AptOnlineShell.{h,cpp}` | `OnlineShell.apt` (CodePrefix `AptOnline`) with retail's pages: `AptOnline::ShellLoadScreen` / `ShellUnloadScreen` make and delete the page objects, which register the retail names (`AptOnline::Login::*` + `AptOnlineLogin::InitGadgets`, `AptOnline::OnlineHome::*` + `AptOnlineHome::InitGadgets`, `AptOnline::CustomMatch::*` + `AptOnlineCustomMatch::InitGadgets`, `GameSorter::OnSort*`). The staging room is AptSkirmish's MpGameSetup handling on the lobby game, as `AptLanLobby` does for the LAN. RW addresses and the BFME2 decomp files are cited in the header; the gaps are stops S-4070 .. S-4076. |
| The transport | `godot/scripts/online_http.gd`, `AptMenuPlayer.set_online / online_take_requests / online_deliver / online_status / online_apply` | Godot's `HTTPRequest` (TLS for https), no redirects, the lobby URL checked as ONLINE-2's client does. |
| The game | `godot/scripts/game.gd` | The main menu's Online (`AptMainMenu::OnlineButtonPressed`) opens the screens on the lobby from `--online-lobby`, `OPENBFME_LOBBY`, Options.ini `OpenBFMEOnlineLobby`, else `https://play.openbfme.com`. The guest name is kept in Options.ini (`OpenBFMEOnlineName`). The staging room's START (`ShellAction::OnlineGameStart`) is played by ONLINE-2's path (`GameWorld.net_online`). `--online-walk=host|join` drives the screens for tests and screenshots. |
| The lobby | `services/online/lobby/src/lobby.mjs` | `GET /v1/motd`: the message of the day (`MOTD`, up to 2,000 bytes, `\n` for line breaks), `null` when none. |

Proof (2026-10-12, the Deck, two Godot clients on a local `server.mjs`):
every screen at 1280 x 800 and 1920 x 1080, START refused without a relay
("Online games can't start yet: no relay server is available."), and with a
local relay both clients loaded the game from the staging room and played 150
frames through it on equal state hashes (0 desyncs).

**Still missing for the owner to test end to end:**
- a live lobby at `https://play.openbfme.com` (CFDEPLOY-1 has the Cloudflare
  deploy, not run yet: the token launcher needs the owner's Windows session)
  with `MOTD` set, and a relay in its `RELAYS` (no VPS yet): until then START
  is refused with the message above;
- the staging room's Ready column draws no tick (MpGameSetup's ready display
  is not ported for LAN either; the ready flag itself works);
- Quick Match, War of the Ring online, Stats and the Messenger (S-4075),
  Create-a-Heroes online (S-4076), ping per relay region (S-4073).

**Rejoin and Watch (round 2, with ONLINE-3 merged).** A restarted game signs
in with the lobby session it kept (ONLINE-3's `user://online_session.txt`),
so the lobby knows it as the same player. The player's running game
(`GET /v1/mine`) is offered in GuiFX's own Yes / No box at sign-in and is a
**REJOIN** row of the Open Play list. Other running games (`GET
/v1/games?running=1`) are **WATCH** rows. The page's own Join Game asks for
the rejoin START (`POST /v1/games/<id>/rejoin`) or a spectator START
(`.../spectate`), and the game plays it as ONLINE-3's `--online-rejoin` /
`--online-spectate` do, with the catch-up line on screen. RotWK's movies
have no Rejoin or Watch button (S-4077). Hot join (a new player in a running
match) is not built: docs/ROADMAP.md, "Hot join", owner decision needed.

Tests: `engine/tests/test_online4_screens.cpp` (the client's checks; the
client and the screens on a local `server.mjs` (Node.js): sign-in, the
message of the day, the list, sort, a password game, create, join from a
second shell, a faction through the lobby, chat, ready, START refused without
a relay, Cancel; and, with a local `openbfme_relay`, the START through the
screens played by two `openbfme_peer` processes), the lobby's
`node --test` (`/v1/motd`).

### Review round 1 (Sol, 2026-10-13): what it found, fixed

- **Credentials in the log.** `game.gd` printed the staging room's whole
  START, relay ticket included. Every text the game prints or shows of the
  lobby now passes `OnlineLobbyClient::redact` (`AptMenuPlayer.online_redact`
  in GDScript): a run of 40 or more base64url characters (a relay, rejoin or
  spectator ticket, a session token's parts) becomes `<redacted>`. A refusal
  that echoes the client's own token is masked whatever the token's shape.
- **Hostile answers.** A 1 MiB message of the day was accepted. The client
  now holds the lobby's own limits: 256 KiB per answer (also Godot's
  `HTTPRequest.body_size_limit` in `online_http.gd` and ONLINE-2's
  `online_lobby.gd`), names 24 / 48 bytes, the map 200, a chat line 256,
  the message of the day 2,000, a START 16 KiB, a refusal's message 512,
  200 games, 200 chat lines. Anything beyond is refused as a protocol error.
- **Recoverable errors.** A malformed START ended the game (`_fail`). The
  screens now read it as `GameWorld.net_online` will (`OnlineStart::parse`)
  before handing it on. If it cannot be played (or the host program cannot
  open the relay socket: `AptMenuPlayer.online_start_failed`), the player
  sees "The online game could not start: ..." and is back at the game list.
  No leave is sent, so the running game is still offered for a rejoin. Lost
  staging room polls are said once in the room's chat ("The lobby does not
  answer: ... (still trying)") and again when the lobby answers.
- **Map names.** The list showed the map's file name. It now shows the map's
  display name, as retail's list does (BFME2 0x59F496: the map cache's entry,
  `MapMetaData::bfme_getBaseDisplayName`, RotWK 0x00300AEA). A map the player
  does not have is shown by its file name and noted.
- **Button tests.** The screen test now uses the movie's own buttons for Join
  Game, the password pop-up's Accept, Refresh, the sorter's Name and Map
  columns (two games), Ready, Play Game and Cancel. Hostile answers and the
  redaction are tested on the client (no lobby needed), on the screens (a
  local lobby with answers rewritten), and in Godot
  (`tools/net/test_online_hostile.py`).

## Phase O3, the reconnect half: what is built (ONLINE-3)

Lane ONLINE-3 (2026-10-12) built the reconnect half of phase O3 (section 6):
disconnects, holds and rejoins through the relay, and a multiplayer pause.
The per-hop relay and the standby relay are not built. **It is a test
feature**, like the rest of online play. LAN games keep RotWK's behaviour
exactly; everything below applies to online games only, except the pause.

| Piece | Where | What it does |
|---|---|---|
| Relay STATUS | `RelayProtocol` (envelope version 3), `RelayCore::tick`, `Transport` | Once a second, while a player is silent or has rejoined, every bound client gets each slot's state: silent (nothing from it for 3 s, `--silent-ms`) and its generation. |
| The ping test | `Network::pingTest`, `DisconnectManager::update` | RotWK's disconnect path (RW 0x8D8A90) blames a silent spell on the silent player unless its ping test (RW 0x8D7C78) fails; then it blames the local player, who quits after five spells. LAN games have no pinger, so the test always passes. Online, the test fails when nothing came from the relay for NetworkDisconnectTime, so a player whose own link died stops blaming the others. |
| Hold and hand-over | `NetworkConfig::holdForRejoin`, `DisconnectManager::updateDisconnectStatus`, `Network::routerRejoinDuties` | When the timeout runs out, the drop is the retail one (one frame, decided by the next packet router). The player is then **held**: their frames are not waited for and their army stays theirs, idle. The packet router hands the army to an ally (retail's DESTROYPLAYER, MSG_SELF_DESTRUCT) when the rejoin window (5 minutes) ends. A Kick vote hands over at once. A vote at the end of the timeout bar counts as a timeout, because the bar's automatic votes come at 0 %. |
| A restarted client | relay: rejoin tickets (version 2: a generation), the REJOINED log record; `Network::checkRelayStatus` | The lobby gives a player whose game restarted a fresh ticket of the next generation (`POST /v1/games/<id>/rejoin`). With it the relay binds the slot's new process without the old session token, starts its streams afresh and reports the new generation. The others then drop the old process at once, without waiting for its timeout. Once the slot is held, they restart their streams to it. |
| Rejoin | `NetGameSession::beginRejoin` / `rejoinStep`, `Network::beginLive`, `fillFromLog`, REJOINREQUEST / REJOIN | The client reads the relay's log of the running game (LOG_REQUEST / LOG_CHUNK) and re-simulates it from frame 0 at full speed, without rendering, while the others play on. Near the live edge it asks to be taken back. The packet router's REJOIN, a frame command, names the frame on every peer alike. The client catches up to that frame and switches to lockstep from the frame after it. It sends its CRCs of the CRC frames whose MSG_LOGIC_CRC executes after the switch, so the next CRC exchange includes it: that exchange proves the rejoined state. A rejoined slot is never packet router or drop decider again. |
| The catch-up path | `GameNetwork/Rejoin.{h,cpp}` `CommandLogCatchUp`, `openbfme_peer --relay-log LOG --online-start START --catch-up F` | The START plus a command log gives the state at frame F, with the players' CRCs in the log compared on the way. Save / resume work can reuse it. |
| The relay log | `RelayCore` (log version 2), `RelayLog::assemble`, `Transport::sendToLog` | The relay also logs the decider's DISCONNECTPLAYER (the agreed drop frame) and the router's REJOIN, so the log alone gives every frame's members. It follows every incarnation of a slot, and maps a rejoined incarnation's CRCs to their frames by execution frame. Drops and rejoins replay exactly (S-3710 updated). A frame command that no live player receives (in a 1v1 whose other player is gone or held, every command of the one left) goes to a **log-only stream**: DATA addressed to the sender's own slot, which the relay logs and acknowledges but forwards nowhere. The relay counts a sender's commands per frame by content, because a log-only stream carries only the tail of a frame. |
| Pause | NETCOMMANDTYPE_PAUSE / RESUME, `Network::requestPause` / `requestResume` | RotWK has **no** multiplayer pause: GameLogic::setGamePaused (RW 0x625AF1) returns at once in a multiplayer game (isInMultiplayerGame, RW 0x441B7C: game mode 1 or 5), and the NetCommandType list (RW 0x989449) has no pause command. OpenBFME adds the smallest one (S-3901): any player sends PAUSE naming its next execution frame, and every peer runs the frames before it and stops. Any player resumes. Pausing works on LAN and online, with no per-player limit. |
| Lobby and clients | `lobby.mjs` (`GET /v1/mine`, `POST /v1/games/<id>/rejoin`), `online_client.py rejoin`, `game.gd` (`--online-rejoin`, the Pause key), `online_lobby.gd` | The game keeps its guest session in the user data; `--online-rejoin` lists the player's running game and rejoins it ("Rejoining the game: catching up" on screen). The Pause key pauses or resumes; the screen says who paused. |

Tests: `engine/tests/test_online3_rejoin.cpp` (the wire formats and the
pinned rejoin ticket, the relay's STATUS / rejoin / log serving, a real
Transport's view of the relay, hold and hand-over, LAN and vote hand-over,
the ping test, pause and resume, and a whole rejoin at the Network level
through a real RelayCore, whose log then replays the whole game; on the
retail game: the catch-up reaches the played state and reports a wrong
CRC), the lobby's node test, `tools/net/test_online_client.py`.

### Measured (2026-10-12)

**End to end with a local relay** (`tools/net/online3_e2e.sh`, JonathanPC,
3 scripted humans + a hard AI on Fall Back 4p, real time, retail
NetworkDisconnectTime 15 s / NetworkPlayerTimeoutTime 60 s). In every run all
peers ran every frame, with one final hash and 0 desyncs. The replay made
from the relay's log matched every CRC frame of the game.

| Scenario | What happened | Result |
|---|---|---|
| rejoin | slot 2 killed (SIGKILL) at 41 s, restarted 21 s later | rejoined at frame 169 after a 1.26 s catch-up; 3 peers on 0xAFE6D77D; the rejoined peer joined 4 CRC checks; replay: 6 CRC frames |
| vote | slot 2 killed; both others pressed Kick | handed over at once (14 objects to the ally); replay: 9 CRC frames |
| window | slot 2 killed, never back; rejoin window 30 s | held, then handed over by the packet router when the window ended, on one frame (15 objects); replay: 9 CRC frames |
| pause | slot 1 paused at frame 200, slot 2 resumed after 8 s | every peer stopped with logic frame 202, for 8.0 s; replay: 9 CRC frames |

**Home lab** (`tools/net/online3_homelab.sh`): the self-hosted lobby and
relay on the pve VM, redeployed with this lane's code; the lobby client
(host, joins, the rejoin START) on the Deck; slots 0 and 1 as headless peers
on pve, and the failing slot as a headless peer on JonathanPC (a second host,
reaching the relay over the LAN). Every replay is the lobby's uploaded copy
of the relay log, turned into a replay and played back on pve.

Each game: 3 scripted humans and a hard AI on Fall Back 4p, 900 frames in
real time (1,500 for "late"), retail timeouts; slot 2's process is killed
(SIGKILL) at 60 s. In every run all peers ended on one hash with 0 desyncs,
and the replay of the lobby's copy of the log matched every CRC frame.

| Run | Hosts | Scenario | Result |
|---|---|---|---|
| 1 | pve + JonathanPC | rejoin: restarted 20 s after the kill | rejoined at frame 278 after a 0.57 s catch-up; 3 peers on 0x8984F5D8; the rejoined peer took part in 6 CRC checks; replay: 9 CRC frames |
| 1 | pve + JonathanPC | late: restarted 110 s after the kill | the player was held when its timeout ran out; the others played on to frame 835, where it rejoined after a 0.90 s catch-up; 3 peers on 0x1A9F305B; replay: 15 CRC frames |
| 1 | pve + JonathanPC | soak: rejoin with every peer's datagrams impaired (80 ± 30 ms, 3 % loss, 1 % duplication, 1 % reordering) | rejoined at frame 282 after a 2.77 s catch-up (the log read is impaired too); 3 peers on 0x83ADFD3A; replay: 9 CRC frames |
| 2 | pve + JonathanPC | vote: both others pressed Kick | handed over at once, 17 objects to the ally; 2 peers on 0xF92B5CB6; replay: 9 CRC frames |
| 3 (final build) | JonathanPC + Deck | rejoin | rejoined at frame 250 after a 1.51 s catch-up on the Deck; 3 peers on 0xAF840AB2; replay: 9 CRC frames |
| 3 (final build) | JonathanPC + Deck | window: never back, rejoin window 30 s | held, then handed over by the packet router when the window ended, 18 objects; 2 peers on 0x9F5DCFFF; replay: 9 CRC frames |
| 3 (final build) | JonathanPC + Deck | pause: slot 1 paused at frame 200, slot 2 resumed after 8 s | every peer stopped with logic frame 202, for 7.6-8.0 s; 3 peers on 0xD59E0181; replay: 9 CRC frames |
| 3 (final build) | JonathanPC + Deck | vote | handed over at once, 17 objects; 2 peers on 0x41D4134A; replay: 9 CRC frames |

Runs 1 and 2 used the build before the log-only stream (below); run 3
used the final build, with JonathanPC as host A because pve's CI slot was
busy with gates (pve still ran the lobby and the relay).

**The game client** (final build), once each, for the proof pictures: the
Godot client on the Deck and a headless host on JonathanPC, through the
pve lobby and relay, 1v1 plus a hard AI. The host paused at frame 200: the
client stopped with logic frame 202 and showed "Game paused by Gondor";
the host resumed 15.6 s later. In a second game the client was killed
(SIGKILL) at frame 250 and started again with `--online-rejoin`. It caught
up in 1.28 s and played on. At frame 1200 the client and the host were both
on 0x2AE10DC1, with 11 CRC checks passed and 0 desyncs.

**What the live runs found** (each fixed, with a test that fails without
the fix):
- A restarted process's first datagrams reached the others before the
  relay's STATUS named its new generation. The old process's disconnect
  screen went off, then came back on the same frame. There, retail's rule
  ignores a repeated disconnect frame (RW 0x8D836B), so the drop was never
  decided. Now the transport's keep-alives don't count while the relay
  reports the slot silent.
- In a 1v1, the player left alone sent its frame commands to nobody once
  the other was held, so the log stopped growing and the rejoiner never saw
  the REJOIN. This led to the log-only stream.
- When the old process was still running as the new one bound, the frame
  commands in flight to it were lost from the log (the relay restarts the
  slot's streams). A peer that finds itself with no live recipient now
  sends its recent frame commands to the log-only stream once more.

**The catch-up time** (`tools/net/online3_catchup.sh`, JonathanPC, 24
threads, load 6-10): a game of 3 scripted humans and a hard AI, played at
full speed through a local relay, re-simulated from its log as a rejoin
does:

| Game length (5 frames/s) | Frames | Catch-up | Frames per second | CRCs compared |
|---|---|---|---|---|
| 10 minutes | 3,000 | 9.1 s | 331 | 30, 0 mismatches |
| 30 minutes | 9,000 | 48.2 s | 186 | 90, 0 mismatches |
| 60 minutes | 18,000 | 105.9 s | 170 | 180, 0 mismatches |

The cost per frame grows with the game (more objects), so the catch-up grows
a little faster than the game's length. A slower PC (a Steam Deck) takes
longer. The others keep playing during the catch-up; they wait only for the
last few frames after the REJOIN.

**Long games (60+ minutes) need snapshots** (phase O4). A deterministic save
of the whole game state at a frame the players agree on (by CRC) would let a
rejoining client load the latest snapshot and re-simulate only the frames
after it. The relay or a verifier would keep one snapshot every few minutes.
That needs the multiplayer save (xfer of every module), which does not exist
yet. Until then, the catch-up of an hour-long game takes about two minutes on
a fast PC.

**Not tested / limits** (S-3900):
- a rejoin during a pause (the rejoiner waits for the resume like everyone);
- a Windows client (JonathanPC ran the Linux peer in WSL);
- the Godot client's rejoin and pause were run once each, for the proof
  pictures; the gates run the headless peer;
- the last player to leave sends its leave to nobody, so a log that closes
  on silence reports that player's end as a crash (S-3710). The replay is
  unaffected.


## Phase O3, spectators: what is built (ONLINE-3, round 2)

The owner asked (2026-10-12) for spectators with no fog of war, through the
relay, reusing the rejoin's catch-up path. **A test feature**, like the rest
of online play.

**Retail first.** RotWK's observer is a lobby slot: the "Observer" faction
(PLAYERTEMPLATE_OBSERVER, -2) is a player of the game that takes part in the
lockstep. Two pieces of it are ported. First, a spectator has no slot, so its
local player is the ReplayObserver side, as retail's replay mode makes it
(GameLogic, RW 0x6282F0). That side's map is permanently revealed
(RW 0x62F91A), so shroud and fog are off and every player's units show.
Second, retail's PlayerList::observeNextPlayer (RW 0x6A8D2B) steps the view
to the next human player (the shroud manager's view player, RW 0xB4F440) and
then back to every player's. The Palantir's observer buttons
(ObserverStuff, SetObserverStuffState RW 0x8003F6) are not driven; a line on
screen shows the observed player instead (S-3902).

| Piece | Where | What it does |
|---|---|---|
| Spectator ticket | `RelayProtocol` ticket version 3 (34 bytes: the spectator index 0..15 in the slot byte, the delay in seconds), `lobby.mjs` `POST /v1/games/<id>/spectate` | Any signed-in account that is not a player gets a spectator START, at the start (it asks again until the host starts) or mid-game. Each call gives a new index, so a spectator that restarted asks again. |
| The relay | `RelayCore::spectatorHello`, `data`, `logRequest`, `tick` | A spectator binds with WELCOME slot 0x80 + index. **Every DATA from a spectator ticket is refused** (REFUSED "a spectator sends no commands"), never logged nor forwarded. It gets no STATUS. It reads the game's log with LOG_REQUEST. The log file is its buffer: nothing is queued per spectator, so a slow spectator only reads later. A spectator silent for 30 s is dropped. A game that ends stays readable while spectators remain (then for its delay plus 2 minutes). |
| The delay | the lobby setup's `spectatorDelay` (0..600 s, default 0), `RelayCore::spectatorLimit` | The relay serves a delayed spectator only the frames at least delay x 5 game frames behind the latest frame any player has logged. A pause holds it. After the game ends, the rest is released at the game's pace. |
| The spectator client | `NetGameSession::beginSpectate` / `spectateStep`, `openbfme_peer` (a spectator START), `GameWorld.net_begin` | No Network: it sends nothing and takes no part in the CRC vote, the run-ahead or the disconnect vote. It catches up from the START + the log (the S-3900 catch-up), then follows the log. It compares its own CRC with every CRC the players logged (a mismatch stops it). Its local input never reaches the game. |
| The view | `LiveGame::observeNextPlayer`, `game.gd` (`--online-spectate=ID`, Tab / Shift+Tab), `GameWorld.net_observed` | "Spectating - every player" or "observing <player>: resources, command points, power (spell book points), rank". |

Tests: `engine/tests/test_online3_spectate.cpp` (the pinned spectator ticket
and START; the relay binds a spectator, refuses its forged command with
nothing logged or forwarded, keeps it out of STATUS, holds the log back by
the delay and keeps a finished game readable; a real Transport's spectator
WELCOME; a late CRC compared; on the retail game: the spectator's hashes
equal the players' for 200 frames, every cell is clear, and observe-next
steps through the humans and back), the lobby's node test,
`tools/net/test_online_client.py`.

### Measured (2026-10-12)

`tools/net/online3_spectate.sh` (the final build): the pve lobby and relay (redeployed), slot 0
and spectators on JonathanPC, slot 1 and spectators on the Deck; 2 scripted
humans and a hard AI on Fall Back 4p, 900 frames in real time. In each run the
players ended on one hash with 0 desyncs, and every spectator stopped at frame
900 on the players' hash, with 8 CRCs compared and 0 mismatches.

| Scenario | What happened | Result |
|---|---|---|
| start | 2 spectators (one per host) asked before the start; the Deck's sent a forged frame command | both on 0x37CCF662; the relay refused the forged command (spectatorDataRefused +1); the players never saw it (one final hash, 0 desyncs) |
| mid | one spectator asked at 60 s; another was killed at 30 s and asked again 15 s later | the late one caught up 300 frames in 0.31 s; the second START was a new place; the relay dropped the dead one (spectatorsDropped +1); all on 0x1667617F |
| delay | host's delay 60 s; spectator and slot 0 on one host | each sampled frame ran 59.5-59.9 s after slot 0's; the relay never served a frame before a player logged 300 frames past it; on 0x755B2F3D |

The Deck's player stalled 37-38 times for one frame (200 ms) per game:
the same as in round 1's home-lab games without spectators.

**After review round 1** (the merged build with the fixes below, the pve lobby
and relay redeployed): pve's CI slot was busy with other lanes' gates, and a
peer waiting there missed the 180 s load wait. So both hosts' processes ran on
the Deck, through pve's lobby and relay over the LAN. All four scenarios
passed. Every spectator stopped at frame 900 on the players' hash, with 8 CRCs
compared and 0 mismatches.

| Scenario | Result |
|---|---|
| start | both spectators on 0x08A53A80; the forged command refused (spectatorDataRefused +1) |
| mid | the late spectator caught up in 0.18 s; the killed one came back on a new place and was dropped by the relay (spectatorsDropped +1); all on 0xF4359219 |
| delay | each sampled frame 59.6-59.8 s behind slot 0; on 0x3E45CD46 |
| delaypause | slot 0 paused before frame 302 for 20.0 s; frames 100-300 ran 79.1-79.3 s behind slot 0 (the delay plus the pause), frames 400-600 59.5-59.6 s; never before the bound; on 0x54F98A16 |

**The game client** (once, for the proof pictures): the Godot client on the
Deck spectating mid-game (two headless players on JonathanPC) caught up 88
frames in 0.61 s and followed live; 10 CRCs compared, 0 mismatches.
Every player's view shows Mordor's base; the same place in Gondor's view is
under its shroud.

**What the live runs found** (each fixed):
- A spectator following the log's edge compared no CRC. The CRC describing
  frame F executes a few frames after F, so the frame had run before its CRC
  reached the log. Now such a frame is compared when a newer log has it
  (a unit test covers it).
- jpc-run runs one job of a lane at a time on a host. The first run's
  spectator on JonathanPC started only after its player had finished, and
  the relay refused it with "game closed". The script now runs a host's
  players and spectators in one command.

**Not tested / limits** (S-3902): a spectator of a LAN game (spectators are
online only); retail's observer slot in an online game; a Windows spectator;
a long game's catch-up grows as a rejoin's does (snapshots, phase O4).

### Review round 1 (Sol, 2026-10-12): what it found, fixed

Sol's review reproduced four defects with local probes. Each fix has a test
that fails with the reviewed code.

1. **Bindings across games.** An address bound to a spectator place that
   then bound a player slot of another game was released as if its spectator
   index were a player slot. That unbound the old game's player in that slot,
   and indices 8..15 wrote past the eight slots. The spectator stayed bound,
   and its later timeout erased the address's new binding. Now an address
   leaves its old binding by kind (spectator place or player slot), and a
   binding is erased only while it is still the one named.
2. **The log-only stream's order.** The first frame command a peer sent with
   no live recipient went to the log before the history it follows (the log
   read 2, 1, 2 for 1, 2). Going alone, live and alone again re-sent the whole
   history. Now the history goes first and no command goes to that stream
   twice, so the relay's count of identical bytes per frame on it is the
   sender's own.
3. **Rebuilt rejoins.** A rejoining client rebuilds the game's earlier drops
   and rejoins from the log. A slot that had rejoined before kept the
   transport its drop retired and an unknown generation, so the relay's STATUS
   made it look restarted again. Now the rebuilt rejoin restarts the slot's
   transport and takes its generation from the log's REJOINED record, as a
   peer that saw it live does. Tested with the same player rejoining three
   times, and with slot 1 then slot 2 rejoining.
4. **Relay limits.** The per-frame command counts and the delay frontier were
   outside the byte caps. Now:
   - the counts are held bytes (64 per node), so the caps end a stream that
     grows them;
   - the frontier is held by its game and thinned beyond 4,096 entries (a
     delayed cut-off only moves earlier), and is looked up by binary search;
   - a game's log stops at 64 MB;
   - a LOG_REQUEST is charged to the client's, the game's and the relay's
     read budgets before the disk is touched.

Also new:
- Retail tests of who owns a dropped player's army at each step: its own
  while held, then the ally's (every object, by id) after the window, a vote
  or a LAN drop, and none to the enemy.
- A delayed spectator during a two-minute pause (the cut-off stands still),
  and the home-lab scenario `delaypause`.

### Review round 2 (Sol, 2026-10-12): what it found, fixed

1. **A rejoin's switch reset its own streams.** The round-1 fix restarted
   the transport of every slot whose earlier rejoin the client rebuilt from
   the log. But the client's streams with that slot were already its current
   incarnation's: they had carried the REJOINREQUEST and frames before the
   switch. Over real UDP, both directions then lost delivery (the mocks
   restart no sequence numbers). Now the rebuilt history changes membership
   and generations only. Once it is applied, the slots gone now are retired;
   no member's stream is touched. Test on real Transports over loopback UDP
   through a real relay: slot 1 rejoins, then slot 2, whose switch rebuilds
   slot 1's rejoin; after each switch every peer passes a CRC exchange.
2. **The caps were checked after the growth.** The relay now admits every
   growth (an early record, the command being joined, a command's count
   nodes, the delay frontier, a disconnect record) under the stream, sender,
   game and relay caps before making it:
   - a command whose counts don't fit stops its stream;
   - once no stream that carried a sender's commands is followed, the
     sender's counts are released and nothing more of it is logged;
   - a frontier that may not grow moves its last entry on;
   - a stopped log releases everything held for it.
   Tested with two games under 128-byte caps (nothing held; round 2 held 160
   bytes, then 320) and under 1,024-byte caps (exactly what fits, checked on
   every datagram).

Both regressions fail with the round-2 code.

Home lab, on the build with these fixes (pve's lobby and relay redeployed):
the spectator scenarios `start`, `mid`, `delay` and `delaypause` passed with
host A on pve and host B on the Deck; every spectator ended on the players'
hash with 8 CRCs compared and 0 mismatches, and the delayed spectator ran
79.2-79.3 s behind before a 20 s pause and 59.6-59.8 s after it. The
reconnect scenario `rejoin` (slots 0 and 1 on pve, slot 2 on the Deck,
killed at 60 s) passed: slot 2 rejoined at frame 251 after a 1.0 s catch-up,
one final hash, and the replay of the lobby's log matched 9 CRC frames.

## 1. What exists today

All of it is LAN multiplayer: peer to peer, IPv4, lockstep at 5 logic frames
a second.

| Piece | Where | What it does |
|---|---|---|
| UDP transport | `GameNetwork/Transport.{h,cpp}` (MP-1, MP-2) | One socket per peer. It keeps a reliable, ordered record stream to every other peer (**full mesh**): CRC-32 + magic + version, cumulative acks, fragmentation within `packetBytes` (1,200 by default: safe for internet MTUs), receive budgets, RFC 6298 resend timing with Karn's backoff, a keep-alive every 100 ms. It checks the sender by its address (`setPeer(slot, address)`). |
| Lockstep | `GameNetwork/Network.{h,cpp}`, `LockstepDriver` | ZH's frame model: a command issued on frame G runs on G + run-ahead on every peer. A FRAMEINFO goes out every frame, and a frame runs once every player's commands for it are in. The state-hash CRC is exchanged every 100 frames. A mismatch triggers a per-subsystem desync report (`DESYNCREPORT`, `DesyncDump`). |
| Packet router and run-ahead | `Network::updateRunAhead`, `NetworkConfig::packetRouterOrder` (MP-2, MP-3) | The *packet router* is a role, not a server. The first live slot in the fallback order measures the round trips and sends RUNAHEAD as a frame command (adaptive between the lobby's value and `maxRunAhead` = 25 frames). The role passes to the next slot when the router leaves; MP-3 r3 derives it from the departure frames alone. Traffic still goes peer to peer. |
| Disconnects | `GameNetwork/DisconnectManager` (MP-2) | Retail's disconnect screen and timeouts (NetworkDisconnectTime, NetworkPlayerTimeoutTime), vote to drop, and drops decided from agreed membership only. A departed player's army goes to an ally. On LAN a peer that drops is gone for the rest of the game; online, ONLINE-3 holds it for a rejoin (see "Phase O3" above). |
| Lobby | `LANAPI`, `LANLobby` (MP-2, UI-1) | Broadcast discovery on ports 8086-8093, the host owns the options, and a START that carries the NewGameMessage, every slot's socket address, the run-ahead and the CRC interval (`LobbyStart`). Driven by the retail `LanLobby.apt` / `MpGameSetup` movies. |
| Identity | `ProfileIdentity` (MP-1) | A canonical text of the engine id, the numeric mode, the RNG and every mounted archive's MD5, compared at JOIN and START. Peers with different data refuse to play. It describes the game files without containing them, so a server can compare it too. |
| Replays | `Common/Recorder` (MP-1, NET-3) | OpenBFME's own replay format: the start, every frame's commands, every frame's state hash. Playback names the first frame that differs. |
| Soaks | `tools/net/mp2_soak.sh`, `tools/net/mp3_soak.sh`, `GameNetwork/NetImpairment` (MP-3) | MP-2: 4-peer 30-minute soaks with loss and jitter, 0 desyncs. MP-3: 8-peer games, 150 ± 50 ms / 5 % loss / duplication / reordering, a router that exits mid-game, fast resend (S-1890 .. S-1892). |

What an internet relay needs on top of this:

1. **NAT and addressing.** Today every peer has to reach every other peer's
   socket address, which fails behind home NATs and CGNAT and exposes
   addresses (the roadmap says players must never see each other's). With a
   relay, every client opens one outbound UDP flow to the relay. The existing
   100 ms keep-alive keeps the NAT mapping alive. The START names the relay
   instead of the peers. Two cheap ways in: (a) *forwarding relay (MVP)*:
   `setPeer(slot, relayAddress)` for every slot, plus a small envelope
   (game id, target slot, session ticket) that the relay strips and forwards.
   The end-to-end transport stays unchanged. (b) *per-hop relay (later)*: the
   client keeps one transport connection to the relay. The relay acks, fans
   each command out once to N-1 players and bundles one downstream datagram per
   frame. Option (a) is a few hundred lines on the client. Option (b) cuts the
   relay's datagram rate and egress three to five times in 8-player games
   (section 2).
2. **Sender authority.** Behind a relay, every peer has the same source
   address, so the transport's "known sender at its address" check turns into
   a slot check. The relay has to stamp or verify each datagram's slot against
   the session that sent it (the lobby's signed ticket). Then a client cannot
   send commands as another slot. The game already takes the player from the
   sending slot, never from the message (ZH `NetGameCommandMsg`).
3. **IPv6.** `NetAddress` is IPv4 only. A relay with a public IPv4 address
   works for every client. IPv6 support for the relay can come later.
4. **Reconnect.** Today a dropped peer is dropped. A rejoin needs the game so
   far: the relay's log (every frame's commands) lets a returning client
   re-simulate from frame 0 at full speed and then rejoin the lockstep at the
   live frame. That is fast enough for short games. Snapshots come with the
   verifier phase, for long ones. On the protocol side this needs a
   "rejoin at frame F" message, plus keeping the slot's player alive (not
   handed to an ally) while the player is away.
5. **The server-side game log.** The relay sees every command record. In the
   MVP it can append the decoded frame commands to a replay
   (`Common/Recorder`'s format; the decoding is in the public `NetPacket`).
   The per-hop relay gets each command exactly once anyway. Upload it to
   object storage when the game ends. The log also gives spectators (a delayed
   read-only stream) and reconnect.
6. **Lobby service.** Replaces LAN broadcast: game list, host, join, chat,
   ready flags, then a START built by the service (same `LobbyStart` fields:
   NewGameMessage, run-ahead, CRC interval, relay address, the per-player
   tickets). The profile identity is compared on the server before a join.

Lockstep and latency: the logic runs at 5 frames a second and the adaptive
run-ahead follows the measured round trip. A relay hop adds a few
milliseconds in-region. A transatlantic game picks one relay, and both sides
see roughly the Atlantic round trip. MP-3 measured games at 150 ± 50 ms with
5 % loss: 2.8 s of stalls per 80 s with fast resend. That is a playable game,
but the stalls under loss are the thing to watch, not bandwidth.

## 2. Bandwidth and cost, measured

### Measurement

`tools/net/online1_bandwidth.sh` (this lane) plays one real-time game
(`openbfme_peer --script --realtime`, run-ahead 2, CRC every 100 frames, clean
localhost links). Every slot is a scripted human: ScriptedPlayer, one action
every 15 frames, which comes to about 30 messages a minute per player. Each
peer reports its transport counters. This lane added
`Transport::Stats::bytesSent / bytesReceived / ackOnlyPacketsSent` and the
report line `transport_bytes`. "Payload" means the UDP payload; the wire adds
28 bytes of IPv4/UDP header per datagram. Runs on JonathanPC, 3,000 logic
frames each, 0 desyncs, every frame on every peer:

| Game | Map | Wall time (fps) | Per directed link and frame | Per link at 5 fps (wire) | Per player up | Whole game (wire) |
|---|---|---|---|---|---|---|
| 2 players | Fords of Isen II | 697.7 s (4.3) | 54.4 B payload, 2.08 datagrams (1.03 ack-only), 1.11 records | 564 B/s | 0.56 KB/s | 1.1 KB/s |
| 4 players | Fall Back 4p | 637.1 s (4.7) | 53.3 B, 2.06 datagrams (1.01 ack-only), 1.11 records | 555 B/s | 1.7 KB/s | 6.7 KB/s |
| 8 players | Fall Back 8p | 829.2 s (3.6) | 54.7 B, 2.15 datagrams (1.10 ack-only), 1.11 records | 574 B/s | 4.0 KB/s | 32.2 KB/s |

Raw totals, 8 players: 9,193,447 payload bytes, 361,012 datagrams (184,412
ack-only), 186,857 records, and `bytes_received` equal to `bytes_sent` within
the in-flight tail. JonathanPC was at load ~160 on 24 cores, so the games ran
below 5 fps. The table therefore normalises per logic frame: every peer sends
a FRAMEINFO per frame per peer, and the acks answer those. This also treats
the 100 ms keep-alive as frame-driven, which is slightly pessimistic.

What the numbers say:
- **One directed link costs about 0.57 KB/s (4.6 kbit/s), whatever the game
  size.** It is mostly protocol framing: a datagram with data and one ack-only
  datagram per frame, at 14 + 28 bytes of headers each. The commands
  themselves average about 11 bytes.
- Mesh traffic grows with N(N-1): an 8-player game moves 28 times the bytes
  of a 1v1.
- Human players issue more orders than ScriptedPlayer. A heavy player at 200
  network messages a minute adds about 35 B per link per frame, which mostly
  rides in datagrams that go out anyway (+30 %). **The budget below uses
  1.5 × the measurement: 0.86 KB/s per directed link.**

### From one game to 1,000 players

Assumptions (stated, not measured):
- Peak: 1,000 players **in games** at the same time. Average over the month:
  **30 % of peak (300 players in games)**, a typical daily curve for a
  community game with a US/EU audience.
- The share of players by game size: 35 % in 1v1, 30 % in 2v2, 15 % in 3v3,
  20 % in 4v4 or 8-player FFA. That is about 90 games at a time on average and
  roughly 160,000 games a month at 25 minutes each.
- MVP forwarding relay: the relay sends each player what the other N-1 peers
  sent to it, so a player's downstream is (N-1) × 0.86 KB/s: 0.86 / 2.6 /
  4.3 / 6.0 KB/s for 2 / 4 / 6 / 8 players.

| Quantity | Value |
|---|---|
| Relay egress per player (mix above) | 2.9 KB/s |
| Peak relay egress (1,000 players) | 2.9 MB/s = **23 Mbit/s** |
| Peak datagram rate at the relay (forwarding) | about 37,000 a second each way (10.8/s per link: 11/s per 1v1 player, 75/s per 8-player player) |
| Monthly relay egress (300 on average) | 300 × 2.9 KB/s × 2.63 M s = **2.3 TB/month** (ingress about the same) |
| Worst case: 1,000 players around the clock, all in 8-player games | 15.8 TB/month |
| Per-hop relay (later): one bundled datagram per frame per player | about 1.1 KB/s down per 8-player player: egress ÷ 3 to 5, datagrams ÷ 7 |
| Lobby / server browser (WebSocket deltas, chat) | under 0.2 TB/month (estimate: 200 B/s per connected client) |
| Replays (measured: an 8-player 3,000-frame game recorded 106,882 bytes, 36,207 gzipped; the per-frame state hash is most of it) | 25-minute game ≤ 270 KB, ≤ 90 KB gzipped → 160,000 games ≈ **14 GB/month** new; about 170 GB after a year |

### Hosting options

Split assumed for two regions: 55 % US, 45 % EU. "3×" is three times the
measured load: 6.9 TB/month and 70 Mbit/s at peak.

**(a) AWS.** Relay on Lightsail: $12 a month for 2 GB RAM with 3 TB of
transfer, in us-east-1 and eu-central-1 [L1]. Lightsail counts **both
inbound and outbound** traffic against the allowance, and charges $0.09/GB
for outbound beyond it [L2]. A relay's inbound roughly equals its outbound,
so 1.27 TB out plus 1.27 TB in on the US box uses 2.5 of its 3 TB. EC2
instead charges $0.09/GB for the first 10 TB out after 100 GB free [A1, A2],
which is about $200 a month at the measured 2.3 TB and kills the budget by
itself. Lobby: API Gateway WebSockets + Lambda + DynamoDB, or on the Lightsail
box. Replays: S3 (about $0.023/GB-month [A3]), downloads through the
CloudFront free plan (100 GB and 1 M requests a month) [A4], or R2.
- Cost at 1×: 2 × $12 + about $5 lobby + about $1 storage ≈ **$30-35/month**.
- At 2-3×: the US box goes past its allowance (2.5 → 7.6 TB counted). Either
  pay $0.09/GB on the excess (about $200+), or move to the $44 (5 TB) and
  $84 (6 TB) bundles: about $130-170.
- Regions: every AWS region, low latency. Ops: low on Lightsail, moderate
  with Lambda + DynamoDB.
- Failure modes: a bill that grows with traffic (a DDoS or a traffic spike is
  billed). Lightsail allowances only pool across instances of the same bundle
  in the same region [L2].
- **Fits only at the measured load.**

**(b) Hetzner or OVHcloud VPS, bandwidth included.**
- *Hetzner*: prices rose on 15 June 2026 [H1]. EU cloud servers include
  20 TB of outbound traffic, US servers 1 TB, and the overage is €1/TB
  [H2, H3]. The cost-optimised CX23 (€5.49) is listed as *currently
  unavailable* [H4]. The available EU line is CPX22 (2 vCPU, 4 GB, €19.49);
  in the US (Ashburn), CPX11 costs €17.49 [H1]. IPv4 costs €0.50 a month
  [H5]. Total ≈ €19.49 + €17.49 + €1 IPv4 + about €1 US overage ≈ **€39
  (about $45)**. At 3×: about €42.
- *OVHcloud*: the VPS page lists VPS-1 at $4.54 (2 vCores, 4 GB, 500 Mbit/s,
  traffic unlimited) and VPS-2 at $8.50 (1 Gbit/s), in US East / US West and
  Europe [O1]. Third-party trackers report a 2026 range at $7.60 / $11.75 [O2],
  so budget for the higher figure. Two VPS-1 come to $9-15 a month, and the
  cost stays flat at 3× (70 Mbit/s at peak is well under 500 Mbit/s).
- Lobby and replays on the same boxes: a small HTTP + WebSocket service and
  local disk with nightly copies off the box.
- Regions: US and EU (Hetzner's US line is pricier and has 1 TB included).
- Ops: you run the OS, TLS, backups and monitoring.
- Failure modes: a single box per region (a reboot drops that region's
  games). Provider anti-DDoS can misclassify UDP floods. Unlimited means fair
  use. Hetzner has stock limits.

**(c) Fly.io regional relays.** A shared-cpu-1x machine with 256 MB costs
$2.19 a month; outbound traffic costs $0.02/GB in North America and Europe; a
dedicated IPv4 costs $2 a month [F1]. UDP **needs a dedicated IPv4 and must
bind `fly-global-services`**, with no public UDP over IPv6 [F2]. Two regions:
2 × ($2.19 + $2) + 2,300 GB × $0.02 ≈ **$55/month**; at 3× ≈ $146. Each extra
region is one command away (South America, Asia-Pacific). Ops: the lowest of
the four, deploys from a Dockerfile. Failure modes: egress-priced, so it
grows with load and attacks; shared-CPU throttling under bursts; UDP has
Fly-specific quirks. **Fits at 1×, not at 3×.**

**(d) Mix: Cloudflare lobby + R2 replays + cheap relay VPSs (recommended).**
- Lobby, browser, sign-in and chat on a Worker plus Durable Objects:
  - Workers Paid is $5 a month, with 10 M requests and 30 M CPU-ms included,
    and no charge for egress [C1].
  - Durable Objects come with Workers Paid: 1 M requests included, then
    $0.15/M; 400,000 GB-s included, then $12.50/M GB-s [C2].
  - The WebSocket Hibernation API means idle lobby sockets cost no duration
    [C2].
  - One Durable Object per region holds the game list. One per game lobby
    holds its chat and slots.
- Replays in R2: $0.015/GB-month, 10 GB free, uploads $4.50 per million,
  reads $0.36 per million, **egress free** [R1].
- Relays: two OVHcloud VPS-1 (US East and EU), or Hetzner CPX22 for the EU.
  The lobby signs a session ticket (Ed25519; the release tooling already
  has `tools/release/ed25519.py`), and the relay checks it offline. If the
  lobby is down, running games are unaffected.
- Cost at 1×: $5 + 2 × $4.54-7.60 + R2 (under $3: 14 GB a month of new replays, 160,000 uploads within the 1 M free Class A operations, downloads free) ≈ **$20-30/month**. At 3×:
  the same plus a third relay ≈ $30-45.
- Regions: the lobby runs at Cloudflare's edge everywhere. The relays sit
  where you put them; adding a region is one more VPS.
- Ops: low for the lobby (managed); moderate for two Linux relays (systemd
  unit, unattended upgrades, a metrics endpoint).
- Failure modes: a Cloudflare outage stops matchmaking but not running games;
  a relay outage drops that region's games (mitigation: a standby relay in the
  same region, which the lobby assigns when health checks fail). A UDP flood
  hits a relay directly; OVH's included anti-DDoS helps, and the relay drops
  datagrams without a valid ticket at line rate.
- **Fits with more than $50 of headroom.**

| Option | 1× (2.3 TB/mo) | 3× (6.9 TB/mo) | Grows with traffic? | Ops |
|---|---|---|---|---|
| (a) AWS Lightsail + Lambda/S3 | ~$30-35 | ~$130-200 | yes, $0.09/GB | low-moderate |
| (a') AWS EC2 | ~$200+ | ~$600+ | yes | moderate |
| (b) Hetzner EU + US | ~$45 | ~$50 | barely (€1/TB) | moderate |
| (b) OVHcloud ×2 | ~$9-15 + lobby | same | no | moderate |
| (c) Fly.io ×2 | ~$55 | ~$146 | yes, $0.02/GB | lowest |
| **(d) Cloudflare + R2 + 2 VPS** | **~$20-30** | **~$30-45** | **no** | low-moderate |

The relay's CPU need is small: about 37,000 datagrams a second each way at
peak in forwarding mode. One thread with `recvmmsg` / `sendmmsg` handles that on one
vCPU, and the per-hop relay cuts the rate by about seven times. Memory:
per-game state plus the log, a few MB per game.

## 3. Accounts and identity

The roadmap requires accounts for online play. All of these options are free
or nearly free:

- **Steam sign-in (OpenID 2.0).** Steam is an OpenID provider at
  `https://steamcommunity.com/openid/`. Sign-in needs no app id; a Web API key
  is needed only for extras such as app ownership checks [S1]. The lobby
  Worker handles the OpenID round trip and keeps the SteamID64. Most of this
  community has a Steam account. Ownership of BFME cannot be checked this way
  (BFME is not sold on Steam), and it isn't needed: the profile identity
  already proves the game files.
- **Discord OAuth2**, scope `identify`: user id and name, free [D1].
- **E-mail magic links**: a Worker plus a transactional mail service, e.g.
  Resend's free tier (3,000 a month, 100 a day; $20 a month for 50,000) [M1].
  It is the fallback for players without Steam or Discord. The 100-a-day
  cap matters on launch day, so keep it as the third option.
- The flow: the client opens the browser to the lobby's sign-in page. The
  page answers through the `openbfme://` handler (section 4) or a localhost
  callback with a session token. The client stores the token in the user
  profile directory, not in the game directory. A display name is chosen
  once and is unique per account.

Anti-abuse basics (from the MVP on):
- Rate limits per account and per IP in the Worker: game creation, joins,
  chat lines, sign-in attempts.
- The lobby is authoritative and validates everything server-side, as
  `LANAPI` does today: slots, factions, colours, teams, start positions,
  map in the published list, profile identity equal for all players.
- The relay accepts only datagrams with a valid ticket for that game and
  slot, and caps bytes per second per slot (10× the measured rate is still
  only about 6 KB/s per link).
- Reports and bans by account. Chat runs through the Durable Object, so it
  can be muted and logged.
- Later, the **verifier**: a headless `openbfme_peer`-style process replays
  the relay's log through the same deterministic simulation. Then results are
  trusted, desyncs can be told apart from tampering, and snapshots enable fast
  rejoin.

## 4. Steam join

**Spacewar (app 480) for a public release: no.** Valve documents 480 as the
SDK's example application: "the example game's AppID is 480", and "for your
games, Valve will assign each an AppID" [S2]. The `steam_appid.txt` that
selects it is a development file: Steam auto-detects the real app id for a
shipped game [S2], and the widely used Steamworks.NET says the file "is not
something that you should be shipping to customers" [S3]. 480 is also the id
pirated games disguise themselves as [S4]. Building OpenBFME's invites on it
would mean:
- every player shows as "Playing Spacewar";
- we depend on an app Valve can change at any time;
- we hand Valve a reason to treat us as misuse of the Steam services, which
  the Subscriber Agreement forbids unless Valve authorises it [S5].

What it would give us: friends-list "Join Game" through the rich presence
`connect` key and invites, where the connect string reaches the game's
command line or arrives as `GameRichPresenceJoinRequested_t` [S6]; and Steam
Datagram Relay for NAT traversal [S7]. The OpenBFME relay already solves NAT,
so the real gain is only the friends-list button.

**The legitimate route, and whether Valve would take it.** A Steam Direct
app costs $100 per title (recoupable after $1,000 of revenue) [S8]. Two
obstacles make acceptance unlikely, honestly:
1. Steam does not publish "content you don't own or have adequate rights
   to" [S9]. OpenBFME ships no EA assets, but it exists only to run EA's Lord
   of the Rings game, whose names and look are EA's and the Tolkien licensors'.
   BFME is not sold on Steam (or anywhere) today, so there is no base game
   for a "requires the original" listing to point at.
2. Licensing: OpenBFME is GPL-3.0, derived from EA's GPL-3.0 release of Zero
   Hour. Valve's own guidance says copyleft licences "will be problematic when
   combining code with the Steamworks SDK. The best-known example is GPL",
   unless the authors grant an exception [S10]. We cannot grant one for EA's
   code. Any Steam integration would therefore have to stay out of the GPL
   binary (a separate, optional helper process speaking a documented local
   protocol), and that still doesn't solve point 1.

**Alternatives that work without Valve:**
- An `openbfme://join/<lobby-id>` URI handler. The launcher registers it
  (Windows registry key under `HKCU\Software\Classes\openbfme`; a `.desktop`
  file with `x-scheme-handler/openbfme` on Linux and the Deck). A link opens
  OpenBFME into that lobby.
- Invite links from the server browser: `https://<lobby domain>/j/<id>`
  opens a web page that offers the `openbfme://` link, or the download.
- Discord rich presence and invites. Invites are "powered entirely by Rich
  Presence" (party size + join secret) and are free; they need a Discord
  application id [D2]. The Discord Social SDK is a proprietary binary. Using
  Discord's local IPC protocol (as the deprecated MIT `discord-rpc` did [D3])
  from a separate helper keeps the GPL binary clean. Players on Steam can add
  OpenBFME as a non-Steam game and paste invite links in Steam chat.

**Verdict:** Steam join is **not feasible legitimately** for a public
OpenBFME release. Using Spacewar is against the purpose Valve gives app 480,
and a real app id is unlikely to be granted (content rights) and conflicts
with the GPL. Build `openbfme://` links and Discord invites; they deliver the
"click to join a friend" experience the owner wants.

## 5. The server browser UI: reuse RotWK's online screens

RotWK 2.01 still carries the complete GameSpy-era online shell in its
archives (one movie per `.big` in the install's `apt` directory; each has a
`.apt`, `.const`, `.dat` and geometry). The game code still names their
callbacks, e.g. `AptOnline::CustomMatch::CreateGame / JoinGame / Refresh`,
`AptOnline::Login::Login`, `AptOnline::OnlineQuickMatch::WidenSearch`. The
online back end in the binary is EA's FESL (`bfme2-ep1-pc.fesl.ea.com`) plus
GameSpy (`GameSpyLogin.ini`, `GameSpyStagingRoom`). Those servers are gone;
the movies are not:

| Movie (RotWK `apt/`) | Size | What it shows (from its clips, buttons and strings) | Reuse |
|---|---|---|---|
| `OnlineShell.apt` | 79,730 | The online frame: nav buttons Home, Open Play, Quick Match, Strategic, Profile/Stats, Messenger button, a chat notice when a friend logs in. Loads the other screens. | yes: the online menu |
| `OnlineLogin.apt` | 65,666 | Sign-in: nickname, e-mail, password, "remember me", country list, Register, Terms of Service, "GameSpy" and official-site buttons. | yes, rewired: Steam / Discord / e-mail buttons in its button slots; nickname and e-mail fields for the magic link |
| `OnlineHome.apt` | 15,080 | The welcome page: message of the day, news ticker, game-type descriptions (Open Play, Tournament, War of the Ring). | yes: MOTD and news from the lobby |
| `OnlineOpenPlay.apt` | 161,042 | **The server browser**: game list with sortable Name / Map / Players / Status columns (`OnSortName`, `OnSortPing`, ...), Refresh, Create Game popup (name, password), Join, password prompt, lobby rooms, the connecting popup, then `MpGameSetup` for the staging room. | yes: the core of the browser |
| `OnlineQuickMatch.apt` | 38,607 | Quick match: side, colour, number of players, connection speed, "searching for players", "players found", widen search, ladder / relative rank. | later: matchmaking phase |
| `OnlineStrategic.apt` | 59,227 | The War of the Ring lobby (`StrategicLobbyTitle`, a load-game button). | later: with War of the Ring |
| `MpGameSetup.apt` | 234,316 | The staging room (shared with LAN, already driven by UI-1/MP-2): player matrix with kick, map and settings, chat, ready, clans tab. | yes: already in use |
| `Messenger.apt` | 123,265 | Friends / buddy list and chat window with tabs, add / remove friend. | later: friends phase |
| `Connections.apt` | 20,206 | Connection quality grid per player pair (excellent / good / poor / failed). | yes: relay link quality |
| `InGameChat.apt` | 25,938 | In-game chat. | already used |
| `Stats.apt` | 35,164 | Per-faction stats with Open Play / Strategic / Tournament tabs. | later: results reporting |

BFME2 1.06 also has `OnlineProfile.apt` and `ScoreScreen.apt`; RotWK drops the
first and keeps its stats in `Stats.apt`.

So **the faithful option is available**. We port the C++ side of these
screens from the binary: the `AptOnline*` callbacks and gadget initialisers
(`AptOnlineCustomMatch::InitGadgets` etc.), the same way UI-1 / MP-2 drove
`LanLobby.apt`. Their data comes from our lobby client instead of GameSpy.
The movies are the player's own files, loaded at runtime, so nothing
retail-format enters the repository. Where a screen offers GameSpy-only
features (clans, the ladder, the GameSpy button), the port hides or disables
those buttons through the movie's own `DisableButton...` / `_disabled`
states. A missing screen is an error report, never a silent fallback. A
mod-friendly OpenBFME-styled screen would only be needed for features the
movies don't have. Spectate and replay downloads are the first candidates;
they go in `MenuExport`-styled frames, `GenericButton_Med` etc.

Flow (all on the retail movies):
1. **Main menu → Online** (`OnlineShell`). Without a session token,
   `OnlineLogin` opens. Sign-in goes through Steam / Discord / e-mail in the
   browser, and the token comes back through `openbfme://auth`.
2. **Home** (`OnlineHome`): MOTD and news from the lobby; the nav goes to
   Open Play.
3. **Browse** (`OnlineOpenPlay`): the region's game list arrives over the
   WebSocket as deltas. Sorting, refresh, a password prompt, and a ping per
   relay region in the status column.
4. **Host**: the Create Game popup (name, optional password) → the lobby
   Durable Object for the game → `MpGameSetup` as host.
5. **Join**: from the list, or from an `openbfme://join/<id>` link →
   profile identity check on the server → `MpGameSetup` as player.
6. **Lobby chat and setup**: `MpGameSetup` chat and slot changes go to the
   game's Durable Object; it validates them and broadcasts.
7. **Start**: the host presses Start when everyone is ready. The lobby sends
   each client a START (`LobbyStart` fields + relay address + ticket);
   `Connections` shows the relay links while loading; lockstep runs through
   the relay.
8. **Post-game**: the relay uploads the game log to R2 as a replay. The
   client shows its local score screen and a "replay saved / view online"
   link. The verifier phase adds trusted results to `Stats`.

## 6. Phased plan

The phases follow ROADMAP "Online multiplayer direction" (relay MVP after the
closed playtest → verifier and snapshots → browser, accounts, spectators →
retail gateway). Estimates are lane-days for one agent lane plus owner time,
and they assume M6's multiplayer work is merged (MP-3 included).

| Phase | Content | Client (public repo) | Service (private) | Estimate |
|---|---|---|---|---|
| O1 Relay MVP | Forwarding relay with tickets; START from a service; the server-side log → replay in R2; `openbfme://join`; a minimal sign-in (Discord or Steam OpenID) | relay envelope in Transport, START from the service, slot-check by session, `openbfme://` handler in the launcher, a debug "join by id" path | Worker + 1 Durable Object lobby, ticket signing, relay (C++, one thread, recvmmsg), R2 upload, two VPSs | 8-12 lane-days + owner: accounts at Cloudflare and one VPS provider |
| O2 Online screens | `OnlineShell`, `OnlineLogin`, `OnlineHome`, `OnlineOpenPlay`, `Connections` ported from the binary, wired to the lobby client; GameSpy-only buttons disabled | the `AptOnline*` ports with RW addresses, tests on the retail movies | game list, chat, MOTD endpoints | 10-15 |
| O3 Reconnect and per-hop relay | rejoin by re-simulating the log; per-hop transport to the relay (bundling, ÷3-5 egress); relay health + standby | rejoin protocol, catch-up at full speed, per-hop mode | relay fan-out, health checks, standby assignment | 8-12 |
| O4 Verifier and snapshots | headless verifier on the relay log; trusted results; snapshots for fast rejoin and multiplayer saves | snapshot format (deterministic save), results message | verifier service (CPU: one game ≈ one core while the game runs; batch after the game is cheaper) | 15-25 |
| O5 Accounts, friends, spectators | all three sign-ins; `Messenger` friends; Discord invites via a helper; delayed spectator stream from the log; `Stats` | Messenger / Stats ports, spectator client mode | friends store (DO SQLite), spectator fan-out | 12-18 |
| O6 Quick match, War of the Ring online | `OnlineQuickMatch`, `OnlineStrategic` | ports | matchmaker | 10-15 |
| O7 Retail gateway | cross-play with retail 2.01 (PLAN's cross-play gates, S-721 retail transport) | retail wire format | gateway | open (depends on M7) |

What the owner has to do (outside any lane): create the Cloudflare and VPS
accounts, own the domain, and keep the service source private.

## Sources

Read 2026-10-10.

- [L1] Amazon Lightsail pricing: https://aws.amazon.com/lightsail/pricing/
- [L2] Lightsail data transfer FAQ (in + out count; $0.09/GB overage; pooling): https://docs.aws.amazon.com/lightsail/latest/userguide/amazon-lightsail-faq-data-transfer-allowance.html
- [A1] EC2 on-demand pricing (100 GB free egress per month): https://aws.amazon.com/ec2/pricing/on-demand/
- [A2] AWS egress tiers ($0.09/GB first 10 TB), third-party summary: https://egresscost.com/aws/ and https://blog.besharp.it/aws-data-transfer-costs-in-a-nutshell/
- [A3] S3 pricing: https://aws.amazon.com/s3/pricing/ (Standard ≈ $0.023/GB-month, summarised at https://infratally.com/articles/aws-s3-pricing-explained-2026/)
- [A4] CloudFront pricing (free plan 100 GB / 1 M requests; flat-rate plans): https://aws.amazon.com/cloudfront/pricing/
- [H1] Hetzner price adjustment 15 June 2026: https://docs.hetzner.com/general/infrastructure-and-availability/price-adjustment/
- [H2] Hetzner Regular Performance (EU 20 TB, US 1 TB included): https://www.hetzner.com/cloud/regular-performance
- [H3] Hetzner traffic overage €1/TB: https://egresscost.com/hetzner and https://docs.hetzner.com/cloud/billing/faq/ (billing in 100 MB blocks)
- [H4] Hetzner Cost-Optimized (CX23 "currently unavailable"): https://www.hetzner.com/cloud/cost-optimized
- [H5] Hetzner IPv4 pricing: https://docs.hetzner.com/general/infrastructure-and-availability/ipv4-pricing
- [O1] OVHcloud VPS: https://www.ovhcloud.com/en/vps/
- [O2] OVHcloud VPS 2026 range, third-party: https://www.vpsbenchmarks.com/hosters/ovhcloud/plans/vps-1
- [F1] Fly.io pricing: https://docs.fly.io/about/pricing
- [F2] Fly.io UDP: https://docs.fly.io/networking/udp-and-tcp
- [C1] Cloudflare Workers pricing: https://developers.cloudflare.com/workers/platform/pricing/
- [C2] Durable Objects pricing: https://developers.cloudflare.com/durable-objects/platform/pricing/
- [R1] Cloudflare R2 pricing: https://developers.cloudflare.com/r2/pricing/
- [S1] Steam OpenID / Web API auth: https://partner.steamgames.com/doc/features/auth
- [S2] Steamworks API example application (SpaceWar, app 480): https://partner.steamgames.com/doc/sdk/api/example
- [S3] Steamworks.NET FAQ (steam_appid.txt not for shipping): https://steamworks.github.io/faq/
- [S4] Spacewar and piracy: https://kotaku.com/thousands-of-people-are-playing-a-hidden-steam-game-1795315575
- [S5] Steam Subscriber Agreement: https://store.steampowered.com/subscriber_agreement/
- [S6] ISteamFriends (rich presence `connect`, join requests): https://partner.steamgames.com/doc/api/ISteamFriends
- [S7] Steam Datagram Relay: https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay
- [S8] Steam Direct fee: https://partner.steamgames.com/doc/gettingstarted/appfee
- [S9] Steamworks onboarding (what you can't publish): https://partner.steamgames.com/doc/gettingstarted/onboarding
- [S10] Distributing open source on Steam (GPL and the SDK): https://partner.steamgames.com/doc/sdk/uploading/distributing_opensource
- [D1] Discord OAuth2: https://docs.discord.com/developers/topics/oauth2
- [D2] Discord Social SDK game invites: https://docs.discord.com/developers/discord-social-sdk/development-guides/managing-game-invites
- [D3] discord-rpc (deprecated, MIT): https://github.com/discord/discord-rpc
- [M1] Resend pricing: https://resend.com/pricing
