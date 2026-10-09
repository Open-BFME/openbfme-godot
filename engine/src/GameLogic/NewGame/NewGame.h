// OpenBFME. GPL-3.0.
//
// The start of a skirmish in the logic (lane START-1): the consumer of the NewGameMessage the lobby's StartGame queued (closes the consumer half of S-178).
//
// WHEN: the message is consumed at logic frame 0, before any logic frame ran: prepareNewGame is called with the message and seeds the logic RNG from GameInfo::seed
// (RotWK InitGameLogicRandom at the lobby's StartGame, RW 0x928736; the generator is the caller's), resolves every random choice in retail's order (SkirmishRandom)
// and returns the resolved GameInfo and the RNG with its state after the draws; LiveGame::load then builds the sides, the players, the map's objects and every
// player's starting base from that (it takes the RNG over, so the draws of the resolution and of the object creation are one stream).  The load screen of the host is
// initialised from the resolved info between the two calls, as retail does (RW 0x62DC3E: resolve, then the load screen).

#pragma once

#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/MapCache.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

#include <string>
#include <vector>

struct NewGameStart
{
	NewGameMessage message;      ///< the message with every random choice resolved
	GameLogicRandom random;      ///< seeded from message.game.seed, advanced by the resolution
	int localSlot = 0;           ///< GameInfo::getLocalSlotNum: the first human slot
	std::string mapName;         ///< LiveGame's map name ("map mp evendim") of the cache key
	std::vector<std::string> notes;
	explicit NewGameStart(RandomAlgorithm algorithm) : random(algorithm) {}
};

namespace NewGame
{
// The map name of a map cache key ("maps/map mp evendim/map mp evendim.map" -> "map mp evendim"); false when the key is not that shape.
bool mapNameOfKey(const std::string &key, std::string &out);
// Consumes `in`: false + *error when the mode is not a skirmish, the map is not in the cache, the game has no player, or the random choices cannot be resolved.
bool prepareNewGame(const NewGameMessage &in, const PlayerTemplateStore &templates, const GameLogicSettings &settings, const std::vector<MapCacheEntry> &maps,
	RandomAlgorithm algorithm, NewGameStart &out, std::string *error);
std::vector<std::string> stopLines();
} // namespace NewGame
