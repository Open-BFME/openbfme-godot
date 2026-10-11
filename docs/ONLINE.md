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
