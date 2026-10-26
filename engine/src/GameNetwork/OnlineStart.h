// OpenBFME. GPL-3.0.
//
// OnlineStart (lane ONLINE-2, docs/ONLINE.md phase O1: "the game START built by the service"): the START an online game's lobby service hands every
// player, read into the LobbyStart a LAN lobby would have produced (GameNetwork/LANLobby.h), so NetGameSession plays an online game exactly as a LAN one,
// with the Transport in relay mode (GameNetwork/Transport.h, RelayProtocol.h).
//
// The service builds the START (services/online/lobby: it owns the slots, picks the seed, sets the run-ahead and CRC interval, names the relay and signs one
// ticket per player); the client only checks and converts it. JSON (small and versioned, the wire of services/online/lobby/src/lobby.mjs):
//   { "v": 1, "kind": "openbfme-start", "game": "<16 hex digits>", "slot": <this player's slot>, "relay": "a.b.c.d:port", "ticket": "<base64url, 32 bytes>",
//     "runAhead": <1 .. 25>, "crcInterval": <0 .. 1000>, "profile": "<the profile identity's MD5, every player's: the service compared them>",
//     "setup": { "map": "<map cache key>", "seed": <u32>, "startingCash": <-1 (lane ONLINE-4: unset, the InitialResources rule) .. 1e9>, "superweaponRestriction": <int>, "rules": null | [10 ints],
//                "legendaryHeroes": <1 .. 4>, "cheatMode": <the cheat seat's slot> (lane CHEAT-1; optional, absent: off), "slots": [ up to 8 x { "state": "open|closed|player|easy|medium|hard|brutal", "name": "<UTF-8>",
//                "faction": <player template index or -1 random>, "color": <-1 ..>, "startPos": <-1 ..>, "team": <-1 ..>, "handicap": <0, -5 .. -95> (lane ONLINEMENU-1; absent: 0),
//                "cah": "<base64 .cah>" (optional) } ] } }
// ONLINE-2 r2: the lobby publishes each game's "openbfme-replay-start" (/v1/replays/<id>/start): the same without "slot", "relay" and "ticket".
// Missing slots are closed. Every field is checked: an unknown version, a missing or mistyped field, a value out of range, a profile other than this
// client's, or a slot that is not a human player's is an error (never a default).
//
// Not simulation code: it produces the NewGameMessage every peer starts from (encoded and resolved by audited code).

#pragma once

#include "GameNetwork/LANLobby.h"

#include <string>

namespace OnlineStart
{
struct Limits
{
	int playerTemplateCount = 0; ///< factions are indices below this (or -1 random)
	int colorCount = 0;          ///< colours are indices below this (or -1 random)
};
enum class Kind
{
	Start,       ///< "openbfme-start": a player's START (its ticket, its slot): what the game and openbfme_peer --online-start play
	ReplayStart, ///< "openbfme-replay-start" (ONLINE-2 r2): the published record of a game for the log converter: no ticket, no slot, no relay
	Spectate     ///< lane ONLINE-3: "openbfme-spectate": a spectator's START (its spectator ticket, "spectator" index and "delay"; no slot): localSlot -1
};
// false + *error. `ownProfile`: this client's identity (its digest must equal the START's). A player's START is never accepted as a replay START and
// the reverse (the lobby publishes replay STARTs to anyone: they must not carry the credentials of a running game)
bool parse(const std::string &json, const ProfileIdentity &ownProfile, const Limits &limits, LobbyStart &out, std::string *error, Kind kind = Kind::Start);
// UTF-8 -> UTF-16 (strict: false on an invalid sequence)
bool utf8ToUtf16(const std::string &in, std::u16string &out);
} // namespace OnlineStart
