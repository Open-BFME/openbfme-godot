# Online play

Online play is not built yet. Today OpenBFME plays skirmish, the campaign
and **LAN** games (the game's Multiplayer menu); the launcher's Multiplayer
page is where the server browser will be.

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

## 1. What exists today

All of it is LAN multiplayer: peer to peer, IPv4, lockstep at 5 logic frames
a second.

| Piece | Where | What it does |
|---|---|---|
| UDP transport | `GameNetwork/Transport.{h,cpp}` (MP-1, MP-2) | One socket per peer. It keeps a reliable, ordered record stream to every other peer (**full mesh**): CRC-32 + magic + version, cumulative acks, fragmentation within `packetBytes` (1,200 by default: safe for internet MTUs), receive budgets, RFC 6298 resend timing with Karn's backoff, a keep-alive every 100 ms. It checks the sender by its address (`setPeer(slot, address)`). |
| Lockstep | `GameNetwork/Network.{h,cpp}`, `LockstepDriver` | ZH's frame model: a command issued on frame G runs on G + run-ahead on every peer. A FRAMEINFO goes out every frame, and a frame runs once every player's commands for it are in. The state-hash CRC is exchanged every 100 frames. A mismatch triggers a per-subsystem desync report (`DESYNCREPORT`, `DesyncDump`). |
| Packet router and run-ahead | `Network::updateRunAhead`, `NetworkConfig::packetRouterOrder` (MP-2, MP-3) | The *packet router* is a role, not a server. The first live slot in the fallback order measures the round trips and sends RUNAHEAD as a frame command (adaptive between the lobby's value and `maxRunAhead` = 25 frames). The role passes to the next slot when the router leaves; MP-3 r3 derives it from the departure frames alone. Traffic still goes peer to peer. |
| Disconnects | `GameNetwork/DisconnectManager` (MP-2) | Retail's disconnect screen and timeouts (NetworkDisconnectTime, NetworkPlayerTimeoutTime), vote to drop, and drops decided from agreed membership only. A departed player's army goes to an ally. **No reconnect:** a peer that drops is gone for the rest of the game. |
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
