// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ZH GameNetwork/GameInfo.h GameInfo and GameSlot reduced to the fields a skirmish uses (8 slots: state, name, colour, start position,
// player template, team; map name, CRC, size, content mask; seed; starting cash).  Moved out of GameClient/GUI/Skirmish/SkirmishSetup.h (lane
// START-1) so the logic that consumes the game (GameLogic/NewGame) does not include the lobby UI: the lobby fills it, the logic resolves it.
// BFME1 SkirmishGameInfo_xfer.cpp lists the same fields plus the "orig*" copies (saved by GameLogic once the random choices are resolved).

#pragma once

#include "Common/CreateAHeroRecord.h"

#include <cstdint>
#include <string>
#include <vector>

constexpr int MAX_SLOTS = 8;              // ZH GameNetwork/GameInfo.h
constexpr int PLAYERTEMPLATE_RANDOM = -1; // ZH Common/PlayerTemplate.h
constexpr int PLAYERTEMPLATE_OBSERVER = -2;

// ZH GameNetwork/GameInfo.h SlotState (the combo item data of the player combos), with BFME2's fourth AI level.
enum SlotState
{
	SLOT_OPEN,
	SLOT_CLOSED,
	SLOT_EASY_AI,
	SLOT_MED_AI,
	SLOT_HARD_AI,
	SLOT_BRUTAL_AI,
	SLOT_PLAYER
};
// BFME2 / RotWK have FOUR AI levels (ZH has three) and the human is state 6: RotWK MpGameSetup reads GUI:HardAI at 0x8419DB with item data 4 and
// GUI:BrutalAI at 0x841A1B with item data 5 (BFME2 0x83F7EA / 0x83F82A); the human predicate 0x8009A7 compares the state with 6; occupied and AI predicates
// cover 2..5.  The numbers are the retail item data and are part of the message.
static_assert(SLOT_EASY_AI == 2 && SLOT_MED_AI == 3 && SLOT_HARD_AI == 4 && SLOT_BRUTAL_AI == 5 && SLOT_PLAYER == 6, "retail slot state numbers");

// lane HERO-2: a slot's Create-a-Hero record is at most this long in its .cah form (the retail records are about 1 KiB; eight of the largest fit one lobby
// datagram with the rest of the game setup)
constexpr std::uint32_t kMaxCreateAHeroBytes = 4096;

struct SkirmishGameSlot
{
	SlotState state = SLOT_CLOSED; // ZH GameSlot::reset(): "decent default"
	std::u16string name;
	bool accepted = false;
	bool hasMap = true;
	int color = -1;          // index into SkirmishSetupSource::colors(), -1 random
	int startPos = -1;       // -1 random
	int playerTemplate = -1; // index into SkirmishSetupSource::factions(), PLAYERTEMPLATE_RANDOM / _OBSERVER
	int teamNumber = -1;     // -1 none, else 0-based team (the combo item data)
	// GameSlot::saveOffOriginalInfo (RotWK 0x800994: orig template +0x2c = template +0x18, orig start +0x28 = start +0x10, orig colour +0x24 = colour
	// +0xc): the lobby's choices (random = -1) before the logic resolved them.  Not part of the lobby's equality.
	int origColor = -1, origStartPos = -1, origPlayerTemplate = -1;
	// lane HERO-2: the slot's Create-a-Hero (RW 0x61B17D reads the game slot's hero + 0x64 when + 0x60 is set): the whole record travels with the game setup
	// (NetPacket::writeGameInfo) and every peer installs it at the game start (LiveGame::load -> CreateAHeroGame); nothing reads a .cah file during play.
	// setCreateAHero keeps the record in its wire form (the record a save / load round trip gives), so the host and the peers hold the same bytes
	bool hasCreateAHero = false;
	CreateAHeroHero createAHero;
	bool setCreateAHero(const CreateAHeroHero &hero, std::string *error)
	{
		const std::vector<std::uint8_t> bytes = hero.save();
		if (bytes.size() > kMaxCreateAHeroBytes)
		{
			if (error)
			{
				*error = "a Create-a-Hero record of " + std::to_string(bytes.size()) + " bytes";
			}
			return false;
		}
		CreateAHeroHero canonical;
		if (!canonical.load(bytes, error))
		{
			return false;
		}
		createAHero = canonical;
		hasCreateAHero = true;
		return true;
	}
	// the record from its .cah form (the wire, a setup file): it must load (the stream, every field, no trailing bytes), match its checksum and be in its wire
	// form (saving it again gives the same bytes); the fields against the Create-a-Hero system are CreateAHeroSystem::validateHero's
	bool setCreateAHeroBytes(const std::vector<std::uint8_t> &bytes, std::string *error)
	{
		auto fail = [&](const std::string &why) {
			if (error)
			{
				*error = "Create-a-Hero: " + why;
			}
			return false;
		};
		if (bytes.size() > kMaxCreateAHeroBytes)
		{
			return fail(std::to_string(bytes.size()) + " bytes");
		}
		CreateAHeroHero h;
		std::string why;
		if (!h.load(bytes, &why))
		{
			return fail(why);
		}
		if (!h.valid || h.save() != bytes)
		{
			return fail("a wrong checksum or not in its wire form");
		}
		createAHero = h;
		hasCreateAHero = true;
		return true;
	}
	void clearCreateAHero()
	{
		createAHero = CreateAHeroHero();
		hasCreateAHero = false;
	}
	void saveOffOriginalInfo()
	{
		origPlayerTemplate = playerTemplate;
		origStartPos = startPos;
		origColor = color;
	}

	bool isHuman() const { return state == SLOT_PLAYER; }
	bool isAI() const { return state >= SLOT_EASY_AI && state <= SLOT_BRUTAL_AI; }
	bool isOccupied() const { return isHuman() || isAI(); }
	bool operator==(const SkirmishGameSlot &o) const
	{
		return state == o.state && name == o.name && accepted == o.accepted && hasMap == o.hasMap && color == o.color && startPos == o.startPos
			&& playerTemplate == o.playerTemplate && teamNumber == o.teamNumber && hasCreateAHero == o.hasCreateAHero && createAHero == o.createAHero;
	}
};

struct SkirmishGameInfo
{
	SkirmishGameSlot slots[MAX_SLOTS];
	std::string mapName; // the map cache key
	std::uint32_t mapCRC = 0;
	std::uint32_t mapSize = 0;
	int mapMask = 0;
	std::uint32_t seed = 0;
	int startingCash = 0;
	int superweaponRestriction = 0;
	bool inProgress = false; // GameInfo::startGame

	// The random choices still in the info: "slot N colour" / "slot N faction" / "slot N start position" for every OCCUPIED slot that has -1 (ZH: observers
	// carry PLAYERTEMPLATE_OBSERVER, never random).  Empty when nothing is left to resolve.
	std::vector<std::string> unresolvedRandomChoices() const
	{
		std::vector<std::string> out;
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			const SkirmishGameSlot &s = slots[i];
			if (!s.isOccupied())
			{
				continue;
			}
			if (s.startPos == -1)
			{
				out.push_back("slot " + std::to_string(i) + " start position");
			}
			if (s.playerTemplate == PLAYERTEMPLATE_RANDOM)
			{
				out.push_back("slot " + std::to_string(i) + " faction");
			}
			if (s.color == -1)
			{
				out.push_back("slot " + std::to_string(i) + " colour");
			}
		}
		return out;
	}

	// RotWK GameInfo::isStartPositionTaken (0x800C20): some slot (occupied or not: no state test) other than `exceptSlot` holds `pos`
	bool isStartPositionTaken(int pos, int exceptSlot) const
	{
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			if (slots[i].startPos == pos && i != exceptSlot)
			{
				return true;
			}
		}
		return false;
	}
	// RotWK GameInfo::isColorTaken (0x800BF6): likewise for the colour
	bool isColorTaken(int colorIndex, int exceptSlot) const
	{
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			if (slots[i].color == colorIndex && i != exceptSlot)
			{
				return true;
			}
		}
		return false;
	}

	int numPlayers() const
	{
		int n = 0;
		for (const SkirmishGameSlot &s : slots)
		{
			n += s.isOccupied() ? 1 : 0;
		}
		return n;
	}
	bool operator==(const SkirmishGameInfo &o) const
	{
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!(slots[i] == o.slots[i]))
			{
				return false;
			}
		}
		return mapName == o.mapName && mapCRC == o.mapCRC && mapSize == o.mapSize && mapMask == o.mapMask && seed == o.seed && startingCash == o.startingCash
			&& superweaponRestriction == o.superweaponRestriction && inProgress == o.inProgress;
	}
};


// ZH GameLogic.h GameMode / Common/GameCommon.h GameDifficulty: the arguments of MSG_NEW_GAME.
enum class NewGameMode
{
	SinglePlayer, // GAME_SINGLE_PLAYER: a solo map chosen in the skirmish list
	Skirmish      // GAME_SKIRMISH
};
constexpr int kDifficultyNormal = 1; // DIFFICULTY_NORMAL ("not really used")

struct NewGameMessage
{
	NewGameMode mode = NewGameMode::Skirmish;
	int difficulty = kDifficultyNormal;
	int rankPoints = 0;
	SkirmishGameInfo game; // TheSkirmishGameInfo at the moment of the message
};

