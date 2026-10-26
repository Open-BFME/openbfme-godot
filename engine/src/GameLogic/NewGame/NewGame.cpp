// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/NewGame.h.

#include "GameLogic/NewGame/NewGame.h"
#include "Common/ExtendedPlayerColors.h"
#include "Common/NumericState.h"

#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/NewGame/SkirmishSides.h"
#include "GameLogic/NewGame/StartingBase.h"

namespace
{
bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}
} // namespace

bool NewGame::mapNameOfKey(const std::string &key, std::string &out)
{
	const size_t lastSlash = key.rfind('/');
	if (lastSlash == std::string::npos || key.size() < 5 || key.compare(key.size() - 4, 4, ".map") != 0)
	{
		return false;
	}
	const std::string stem = key.substr(lastSlash + 1, key.size() - 4 - (lastSlash + 1));
	const size_t prevSlash = key.rfind('/', lastSlash - 1);
	if (prevSlash == std::string::npos || prevSlash == lastSlash || key.compare(0, prevSlash + 1, "maps/") != 0)
	{
		return false;
	}
	const std::string dir = key.substr(prevSlash + 1, lastSlash - prevSlash - 1);
	if (dir != stem)
	{
		return false;
	}
	out = dir;
	return true;
}

bool NewGame::prepareNewGame(const NewGameMessage &in, const PlayerTemplateStore &templates, const GameLogicSettings &settings, const std::vector<MapCacheEntry> &maps,
	RandomAlgorithm algorithm, NewGameStart &out, std::string *error)
{
	// the game start is lockstep state: whatever FP environment the caller (GUI, network) left must not reach the random resolution (retail setFPMode)
	NumericState::normalizeFloatingPointEnvironment();
	if (in.mode != NewGameMode::Skirmish)
	{
		return failWith(error, "only a skirmish new-game message is consumed (a single player map game is the campaign lane's)");
	}
	NewGameStart start(algorithm);
	start.message = in;
	SkirmishGameInfo &info = start.message.game;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (info.slots[i].state < SLOT_OPEN || info.slots[i].state > SLOT_PLAYER)
		{
			return failWith(error, "slot " + std::to_string(i + 1) + ": unknown slot state");
		}
	}
	if (info.numPlayers() == 0)
	{
		return failWith(error, "the game has no occupied slot");
	}
	const MapCacheEntry *entry = nullptr;
	for (const MapCacheEntry &m : maps)
	{
		if (m.name == info.mapName)
		{
			entry = &m;
			break;
		}
	}
	if (!entry)
	{
		return failWith(error, "the map '" + info.mapName + "' is not in the map cache");
	}
	if (!mapNameOfKey(info.mapName, start.mapName))
	{
		return failWith(error, "the map cache key '" + info.mapName + "' is not maps/<name>/<name>.map");
	}
	if (!MapCache::validateStartPositions(*entry, error))
	{
		return false;
	}
	start.mapPlayerCount = entry->numPlayers;
	info.mapStartCount = entry->numPlayers > LEGACY_MAX_SLOTS ? entry->numPlayers : LEGACY_MAX_SLOTS;
	bool assigned[MAX_SLOTS]{};
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER) continue;
		if (slot.startPos < -1 || slot.startPos >= entry->numPlayers)
		{
			return failWith(error, "slot " + std::to_string(i + 1) + ": invalid explicit start position " + std::to_string(slot.startPos));
		}
		if (slot.startPos >= 0)
		{
			if (assigned[slot.startPos])
			{
				return failWith(error, "slot " + std::to_string(i + 1) + ": duplicate explicit start position " + std::to_string(slot.startPos));
			}
			assigned[slot.startPos] = true;
		}
	}
	start.localSlot = -1;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (info.slots[i].isHuman())
		{
			start.localSlot = i;
			break;
		}
	}
	// InitGameLogicRandom(seed): TheGlobalData +0x1228 (the fixed seed, -1 unless the command line set one) is not set here
	start.random.initGameLogicRandom(info.seed, -1);
	SkirmishResolveInput input;
	input.templates = &templates;
	input.numColors = ExtendedPlayerColors::count((int)settings.multiplayerColors.size(), info.mapStartCount);
	input.map = entry;
	std::string why;
	if (!SkirmishRandom::resolve(info, input, start.random, &why))
	{
		return failWith(error, "resolving the random choices: " + why);
	}
	if (start.localSlot < 0)
	{
		start.notes.push_back("no human slot: the observer's ReplayObserver side is the local player");
	}
	out = std::move(start);
	return true;
}

std::vector<std::string> NewGame::stopLines()
{
	std::vector<std::string> out = SkirmishRandom::stopLines();
	for (const std::string &s : SkirmishSides::stopLines())
	{
		out.push_back(s);
	}
	for (const std::string &s : StartingBase::stopLines())
	{
		out.push_back(s);
	}
	return out;
}
