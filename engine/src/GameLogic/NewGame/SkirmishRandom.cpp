// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/SkirmishRandom.h for the sources of every rule.

#include "GameLogic/NewGame/SkirmishRandom.h"
#include "Common/NumericState.h"

#include "GameLogic/SimMath.h"

#include <cstdio>

namespace
{
constexpr const char *kFile = "GameLogic.cpp"; // the call log's file name; the retail argument is the full build path (not part of the draw)

bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

// RW 0x62D83C ff: the start spot distance of two positions (float32 squares and sum in PC24 = float32 rounding, the CRT sqrt of the double, stored as float32)
float distanceOf(const Coord3D &p1, const Coord3D &p2)
{
	const float dy = SimMath::subf32(p1.y, p2.y);
	const float dx = SimMath::subf32(p1.x, p2.x);
	const float dy2 = SimMath::mulf32(dy, dy);
	const float dx2 = SimMath::mulf32(dx, dx);
	const float sum = SimMath::addf32(dy2, dx2);
	return (float)SimMath::sqrtd((double)sum);
}
} // namespace

bool SkirmishRandom::populateRandomStartPosition(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error)
{
	int numPlayers = MAX_SLOTS;
	if (in.map)
	{
		numPlayers = in.map->numPlayers;
	}
	if (numPlayers < 0 || numPlayers > MAX_SLOTS)
	{
		return fail(error, "the map cache entry has numPlayers " + std::to_string(numPlayers) + " (1.." + std::to_string(MAX_SLOTS) + " expected)");
	}
	// the distance matrix [i][j] (RW stack [ebp + (i * 8 + j) * 4 - 0x15C])
	float dist[MAX_SLOTS][MAX_SLOTS];
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		for (int j = 0; j < MAX_SLOTS; ++j)
		{
			if (in.map && i != j && i < numPlayers && j < numPlayers)
			{
				const auto a = in.map->startPositions.find(i + 1);
				const auto b = in.map->startPositions.find(j + 1);
				if (a == in.map->startPositions.end() || b == in.map->startPositions.end())
				{
					dist[i][j] = 1000000.0f; // RW 0xBDCDC0
				}
				else
				{
					dist[i][j] = distanceOf(a->second, b->second);
				}
			}
			else
			{
				dist[i][j] = 0.0f;
			}
		}
	}
	bool hasStartSpotBeenPicked = false;
	bool taken[MAX_SLOTS];
	int posOwner[MAX_SLOTS];
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		taken[i] = i >= numPlayers;
		posOwner[i] = -1;
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			continue;
		}
		if (slot.startPos >= 0 && slot.startPos < numPlayers)
		{
			hasStartSpotBeenPicked = true;
			taken[slot.startPos] = true;
			posOwner[slot.startPos] = i;
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		SkirmishGameSlot &slot = info.slots[i];
		bool teamHit = false; // RW [ebp - 0x1D]: reset per slot only
		if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			continue;
		}
		int pos = slot.startPos;
		if (pos >= 0 && pos < numPlayers)
		{
			continue;
		}
		if (hasStartSpotBeenPicked)
		{
			int bestIdx = -1;
			float bestVal = 0.0f;
			for (int cand = 0; cand < numPlayers; ++cand)
			{
				if (taken[cand])
				{
					continue;
				}
				if (bestIdx < 0)
				{
					bestIdx = cand;
					for (int n = 0; n < numPlayers; ++n)
					{
						if (taken[n] && n != cand)
						{
							bestVal = SimMath::addf32(dist[cand][n], bestVal);
						}
					}
				}
				else
				{
					float sum = 0.0f;
					for (int n = 0; n < numPlayers; ++n)
					{
						if (!taken[n] || n == cand)
						{
							continue;
						}
						if (slot.teamNumber > -1 && info.slots[posOwner[n]].teamNumber == slot.teamNumber)
						{
							teamHit = true;
							if (bestVal > dist[cand][n])
							{
								bestVal = dist[cand][n];
								bestIdx = cand;
							}
						}
						else if (!teamHit)
						{
							sum = SimMath::addf32(dist[cand][n], sum);
							if (sum > bestVal)
							{
								bestVal = sum;
								bestIdx = cand;
							}
						}
					}
				}
			}
			if (bestIdx < 0)
			{
				return fail(error, "slot " + std::to_string(i) + ": no free start position is left");
			}
			slot.startPos = bestIdx;
			taken[bestIdx] = true;
			posOwner[bestIdx] = i;
		}
		else
		{
			if (pos != -1)
			{
				return fail(error, "slot " + std::to_string(i) + " has the start position " + std::to_string(pos) + " outside 0.." + std::to_string(numPlayers - 1));
			}
			if (numPlayers < 1)
			{
				return fail(error, "the map has no start position");
			}
			while (pos == -1)
			{
				pos = rng.getValue(0, numPlayers - 1, kFile, 0x879);
				if (info.isStartPositionTaken(pos, -1))
				{
					pos = -1;
				}
			}
			slot.startPos = pos;
			taken[pos] = true;
			posOwner[pos] = i;
			hasStartSpotBeenPicked = true;
		}
	}
	// observers: RW 0x62DB99 ff
	int numPlayersInGame = 0;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (info.slots[i].isOccupied() && info.slots[i].playerTemplate != PLAYERTEMPLATE_OBSERVER)
		{
			++numPlayersInGame;
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied() || slot.playerTemplate != PLAYERTEMPLATE_OBSERVER)
		{
			continue;
		}
		int pos = 0;
		if (numPlayersInGame != 0)
		{
			if (numPlayers < 1)
			{
				return fail(error, "the map has no start position");
			}
			do
			{
				pos = rng.getValue(0, numPlayers - 1, kFile, 0x89E);
			} while (!info.isStartPositionTaken(pos, -1)); // retail retries until it lands on a position some slot holds
		}
		slot.startPos = pos;
	}
	return true;
}

bool SkirmishRandom::populateRandomSideAndColor(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error)
{
	if (!in.templates)
	{
		return fail(error, "no PlayerTemplate store");
	}
	const PlayerTemplateStore &store = *in.templates;
	const int count = store.getPlayerTemplateCount();
	std::vector<int> startSlots;
	for (int i = 0; i < count; ++i)
	{
		const PlayerTemplate *pt = store.getNthPlayerTemplate(i);
		if (pt && pt->m_playableSide)
		{
			startSlots.push_back(i);
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied())
		{
			continue;
		}
		int idx = slot.playerTemplate;
		while (idx != PLAYERTEMPLATE_OBSERVER && (idx < 0 || idx >= count))
		{
			const std::uint32_t silly = rng.baseSeed() % 7u; // GetGameLogicRandomSeed() (RW 0x6D3204)
			for (std::uint32_t k = 0; k < silly; ++k)
			{
				rng.getValue(0, 1, kFile, 0x786);
			}
			// the candidates: startSlots (the per-position faction set of the map cache and the slot's create-a-hero set filter them in retail: S-270)
			const std::vector<int> &candidates = startSlots;
			if (candidates.empty())
			{
				return fail(error, "slot " + std::to_string(i) + ": the PlayerTemplate store has no playable faction to choose from");
			}
			const int r = rng.getValue(0, 1000, kFile, 0x7C7);
			const int chosen = candidates[(std::uint32_t)r % (std::uint32_t)candidates.size()];
			const PlayerTemplate *pt = store.getNthPlayerTemplate(chosen);
			if (pt && pt->m_playableSide)
			{
				slot.playerTemplate = chosen; // GameSlot::setPlayerTemplate (RW 0x802C5A): the store index (the campaign branches do not apply to a skirmish)
				idx = chosen;
			}
			else
			{
				idx = -1;
			}
		}
		int colorIdx = slot.color;
		if (colorIdx < 0 || colorIdx >= in.numColors)
		{
			if (in.numColors < 1)
			{
				return fail(error, "slot " + std::to_string(i) + ": multiplayer.ini defines no colour to choose from");
			}
			colorIdx = -1;
			while (colorIdx == -1)
			{
				colorIdx = rng.getValue(0, in.numColors - 1, kFile, 0x7DE);
				if (info.isColorTaken(colorIdx, -1))
				{
					colorIdx = -1;
				}
			}
			slot.color = colorIdx;
		}
	}
	return true;
}

bool SkirmishRandom::resolve(SkirmishGameInfo &info, const SkirmishResolveInput &in, GameLogicRandom &rng, std::string *error)
{
	NumericState::normalizeFloatingPointEnvironment(); // also callable on its own (tests, a future network lobby): same canonical environment
	for (SkirmishGameSlot &slot : info.slots)
	{
		slot.saveOffOriginalInfo();
	}
	return populateRandomStartPosition(info, in, rng, error) && populateRandomSideAndColor(info, in, rng, error);
}

std::vector<std::string> SkirmishRandom::stopLines()
{
	return {
		"[S-270] skirmish random resolution: ported from the RotWK 2.01 disassembly (RW 0x62D7EA, 0x62D44C; S-001 caveat, no executed oracle: the retail oracles are Windows only); "
		"the per-start-position faction set of the map cache record and the create-a-hero set of a slot are not read (the retail mapcache.ini carries neither), a start "
		"position outside the map's range is an error where retail writes past its array, and the RNG is the generator named by the caller (S-080)",
	};
}
