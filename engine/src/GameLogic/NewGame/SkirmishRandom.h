// OpenBFME. GPL-3.0.
//
// The resolution of the lobby's random choices at game start (lane START-1; closes the consumer half of stop S-178): populateRandomStartPosition then
// populateRandomSideAndColor, in retail's order, with every discard / retry draw of the logic RNG.  LOCKSTEP-CRITICAL: every peer runs this on the same
// GameInfo with the same seeded RNG and must end with the same slots and the same RNG state.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly, no executed oracle: the Windows retail oracles cannot run on this machine):
//   * the caller RW 0x62DC3E (GameLogic prepareNewGame): for every slot GameSlot::saveOffOriginalInfo (0x800994), then populateRandomStartPosition
//     (0x62D7EA), then populateRandomSideAndColor (0x62D44C), then the load screen is made and initialised.  (ZH / BFME1 call them in the other order.)
//   * InitGameLogicRandom(GameInfo seed) (0x6D3261, seed at GameInfo + 0x50) runs in the lobby's StartGame (RW 0x928736); here the consumer seeds the RNG
//     from the message, which is the same value at the same logical point (before any draw).
//   * populateRandomStartPosition 0x62D7EA:
//       - numPlayers = the map cache entry's numPlayers (MapMetaData + 0x20); 8 without an entry.  startSpotDistance[i][j] for i != j, both < numPlayers: the
//         waypoint map's Player_%d_Start of both; a missing one gives 1,000,000 (RW 0xBDCDC0); else sqrt(dy*dy + dx*dx) (x87 at PC24 for the squares and the
//         sum, the CRT sqrt, stored as float32); every other entry 0.
//       - taken[i] = i >= numPlayers; slots that are occupied, not observers and hold a start position in [0, numPlayers) reserve it (ZH's `>= 0 ||` is
//         fixed here) and remember their slot as the owner; hasStartSpotBeenPicked is set by any of them.
//       - then for every occupied non-observer slot in ascending order whose position is not in [0, numPlayers): with no position picked yet it draws
//         GetGameLogicRandomValue(0, numPlayers-1) until the position is not held by another slot (isStartPositionTaken(pos, -1)); otherwise it takes the
//         candidate with the best score: the first free candidate scores the SUM of its distances to the taken positions; every later free candidate runs
//         over the taken positions n (ascending): for a slot with a team, a position whose owner is on the same team sets teamHit and takes the candidate
//         when the running best is larger than that single distance; any other position (when no team hit happened yet for this SLOT: the flag is not
//         reset per candidate) adds its distance to the candidate's running sum and takes the candidate when the sum exceeds the best.  Floats are float32
//         with SSE order.  ZH's per-team remembered position is NOT in the retail code.
//       - observers last: with nobody playing the position is 0; else GetGameLogicRandomValue(0, numPlayers-1) until isStartPositionTaken(pos, -1).
//   * populateRandomSideAndColor 0x62D44C: startSlots = the store's templates in store order whose PlayableSide (+0x151) is set.  Per occupied slot in
//     ascending order: while the template is not observer (-2) and not a valid store index: discard (GetGameLogicRandomSeed() % 7) draws of
//     GetGameLogicRandomValue(0, 1), take the candidates (startSlots filtered by the start position's faction set of the map cache record, and by the slot's
//     create-a-hero set; neither exists in the retail map cache, see S-27x), draw GetGameLogicRandomValue(0, 1000) % candidateCount, keep the template when it
//     is PlayableSide (else -1 and again); then a colour outside [0, numColors) draws GetGameLogicRandomValue(0, numColors - 1) until no slot holds it
//     (isColorTaken(colour, -1)).  The faction of a slot is resolved BEFORE its colour, slot by slot.
//   * GetGameLogicRandomSeed() is the base seed of GameLogicRandom (RW 0x6D3204 = [0xDE4A68], set by InitGameLogicRandom).
// Not ported: the faction set per start position of the map cache and the create-a-hero set of a slot (neither is read from the retail data here); an
// occupied slot whose position is out of range but not -1 is an error here (retail writes past its array).

#pragma once

#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/MapCache.h"
#include "GameNetwork/GameInfo.h"

#include <string>
#include <vector>

struct SkirmishResolveInput
{
	const PlayerTemplateStore *templates = nullptr;
	int numColors = 0;                 ///< the number of MultiplayerColor blocks
	const MapCacheEntry *map = nullptr; ///< the cache entry of the game's map (retail reads numPlayers and the Player_N_Start waypoints from it)
};

namespace SkirmishRandom
{
// The slot loop's saveOffOriginalInfo, populateRandomStartPosition and populateRandomSideAndColor on `info`, drawing from `rng`.  False + *error when the
// input cannot be resolved (no playable template, no colour, no map entry, a start position out of range ...); `info` is then partly changed.
bool resolve(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error);
// the two steps on their own (tests pin them separately)
bool populateRandomStartPosition(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error);
bool populateRandomSideAndColor(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error);

// The stop lines of this consumer (STOPS.md S-178 / S-27x), for the reports
std::vector<std::string> stopLines();
} // namespace SkirmishRandom
