// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/NewGame.h.

#include "GameLogic/NewGame/NewGame.h"

#include "Common/ExtendedPlayerColors.h"
#include "Common/NumericState.h"
#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/NewGame/SkirmishSides.h"
#include "GameLogic/NewGame/StartingBase.h"

#include <algorithm>
#include <vector>

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
	bool sharedStarts = false; // a War of the Ring battle's allies on one position
	if (in.livingWorldBattle.active)
	{
		// lane WOTR-2 [S-1329 inference] (review r2): a War of the Ring battle has as many participants as the region's fight (allies too) on a map with its
		// own number of start positions. When they outnumber the positions, the positions go by side: the k-th team (in slot order) takes position k and
		// its players share it (allies side by side); more sides than positions is refused by name (retail's placement of a battle's players was not read)
		int participants = 0;
		std::vector<int> sides; // team numbers, first appearance in slot order
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			const SkirmishGameSlot &slot = info.slots[i];
			if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				continue;
			}
			++participants;
			if (std::find(sides.begin(), sides.end(), slot.teamNumber) == sides.end())
			{
				sides.push_back(slot.teamNumber);
			}
		}
		if (participants > (int)entry->numPlayers)
		{
			if ((int)sides.size() > (int)entry->numPlayers)
			{
				return failWith(error, "the War of the Ring battle on " + info.mapName + " has " + std::to_string(sides.size()) + " sides for the map's " +
					std::to_string(entry->numPlayers) + " start positions");
			}
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				SkirmishGameSlot &slot = info.slots[i];
				if (slot.isOccupied() && slot.playerTemplate != PLAYERTEMPLATE_OBSERVER)
				{
					slot.startPos = (int)(std::find(sides.begin(), sides.end(), slot.teamNumber) - sides.begin());
				}
			}
			sharedStarts = true;
			start.notes.push_back("[S-1329] " + std::to_string(participants) + " battle players on " + std::to_string(entry->numPlayers) +
				" start positions: allies share their side's position");
		}
	}
	// the explicit start positions against the map's (after a War of the Ring battle's shared positions; a War of the Ring campaign's positions are
	// region indices, checked by its lobby, GUI:NeedStartSpots)
	if (in.livingWorldCampaign.empty())
	{
		bool assigned[MAX_SLOTS]{};
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			const SkirmishGameSlot &slot = info.slots[i];
			if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER) continue;
			if (slot.startPos < -1 || slot.startPos >= entry->numPlayers)
			{
				return failWith(error, "slot " + std::to_string(i + 1) + ": invalid explicit start position " + std::to_string(slot.startPos));
			}
			if (slot.startPos >= 0 && !sharedStarts)
			{
				if (assigned[slot.startPos])
				{
					return failWith(error, "slot " + std::to_string(i + 1) + ": duplicate explicit start position " + std::to_string(slot.startPos));
				}
				assigned[slot.startPos] = true;
			}
		}
	}
	if (!in.livingWorldCampaign.empty())
	{
		// lane WOTR-2: a War of the Ring campaign's start positions are region indices of the scenario's region campaign that every player picked on the
		// setup's map preview (the lobby refuses a slot without one, GUI:NeedStartSpots): no map spot is resolved, and the lobby's map plays no part
		input.map = nullptr;
		input.strategicStarts = true;
	}
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
