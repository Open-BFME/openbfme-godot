// OpenBFME. GPL-3.0.
//
// EconomySettings and EconomyContext (lane ECON-1): the GameData values the economy reads and the game state it depends on.
//
// TARGET FACTS (RotWK game.dat GlobalData = RW 0xDE4364, caveat S-001; the field-table rows were read at the INI names' string xrefs):
//   TerrainResourceCellSize  parseReal  +0xD8     GoodCommandPoints +0xDEC / EvilCommandPoints +0xDF4 (two ints: start, cap)
//   GoodCommandPointsBonus +0xDFC, EvilCommandPointsBonus +0xE00 (ints)       GoodCommandPointsAI +0xE04 / EvilCommandPointsAI +0xE0C (two ints)
//   GoodCommandPointsMP2..MP8 +0xE14 + 0x10 * (n - 2), EvilCommandPointsMPn +0xE1C + 0x10 * (n - 2) (two ints each; parse RW 0x641911 = two parseInt)
//   ResourceBonusMultiplier +0xE88 (real)  GoodCommandPointLimit +0xE8C / EvilCommandPointLimit +0xE90 (ints)  PowerLimit +0xE94 (int)  ResourceMultiplierLimit +0xE98 (real)
//   MultiPlayMoneyMult +0xEC4 (RW 0x642182: `MPn:value` tokens, n = 1..8, compared with _stricmp, stored at 4 * (n - 1); an index lookup RW 0x642002 reads
//   [0, 20) and gives 1.0 outside)
// Not read here (campaign / Living World, M9): StartingCashRTS and the regions' resource bonuses; the Good/Evil CommandPointLimit / PowerLimit /
// ResourceMultiplierLimit / ResourceBonusMultiplier values are loaded (they are GameData) but nothing in the skirmish game consumes them.
//
// EconomyContext is what RW keeps in GameLogic (+0x110 game mode, +0x114 kind, +0x98 keep score) and TheGameInfo (+0x6C lobby percentage):
//   * gameMode (+0x110): 1 LAN, 2 SKIRMISH, 5 INTERNET (RW 0x441B7C / 0x625456 read it); the multiplayer-money and command-point rules apply to those three;
//   * gameKind (+0x114): RW 0x6A7C86 uses the multiplayer command point tables when it is 3 (and 0x602E64's handicap is off when it is 3 and the Living World
//     flag is clear). That the skirmish game sets 3 is INFERENCE (the value is written by the new-game message handler RW 0x779CB4 from message argument 1
//     and reset to 3 by RW 0x779F20; the sender of a skirmish game was not located): stop S-254;
//   * livingWorld: RW 0xDE4950 + 0xB4 (and + 0xB5): the strategic game (never in a skirmish);
//   * lobbyCommandPointPercent: TheGameInfo + 0x6C (100 unless the lobby changed the army size option); gameInfoPresent: TheGameInfo exists;
//   * scoring: GameLogic + 0x98 (the keep-score switch of ScoreKeeper).

#pragma once

#include "Common/StateHash.h"

#include <string>

class ArchiveFileSystem;

struct EconomySettings
{
	struct Pair
	{
		int start = 0;
		int cap = 0;
	};
	bool loaded = false;
	float terrainResourceCellSize = 0.0f;
	Pair goodSolo, evilSolo, goodAI, evilAI;
	Pair goodMP[7], evilMP[7]; ///< players 2 .. 8
	int goodBonus = 0, evilBonus = 0;
	int goodLimit = 0, evilLimit = 0, powerLimit = 0;
	float resourceBonusMultiplier = 0.0f, resourceMultiplierLimit = 0.0f;
	float multiPlayMoneyMult[20]; ///< MP1 .. MP8 as parsed; the rest 1.0 (INFERENCE: GlobalData's constructor value, S-255)
	// lane XP-1: MultiPlayUnitXPMult (row RW 0xC00C8C, parse RW 0x6422D2, GlobalData + 0xEC4 + 0x50) and MultiPlayBuildingXPMult (RW 0xC00C9C, RW 0x642408,
	// + 0xA0): the same `MPn:value` grammar; the lookups RW 0x64201C / 0x642037 read index n - 1 in [0, 20), 1.0 outside. Not required (1.0 when absent:
	// the same constructor inference, S-255)
	float multiPlayUnitXPMult[20];
	float multiPlayBuildingXPMult[20];
	// lane PLAY-1: NumMinutesBeforePlayersCanTransferMoney (row RW 0xC011E0, parseInt RW 0x42EC5E, GlobalData + 0x122C; the constructor's value 5, RW 0x6439B7):
	// the tribute (MSG_GIVE_MONEY, RW 0x6264E1) is refused before minutes * LOGICFRAMES_PER_SECOND * 60 logic frames (RW 0x626087). Not required (the constructor
	// value when absent); in crc() (Sol review: a peer whose gate differs must not hash equal; the tribute's effects, cash and ScoreKeeper, are hashed too)
	int numMinutesBeforePlayersCanTransferMoney = 5;
	EconomySettings()
	{
		for (float &f : multiPlayMoneyMult)
		{
			f = 1.0f;
		}
		for (int i = 0; i < 20; ++i)
		{
			multiPlayUnitXPMult[i] = 1.0f;
			multiPlayBuildingXPMult[i] = 1.0f;
		}
	}

	// reads data\ini\gamedata.ini through the shared INI pipeline (macros, retail field parsers, later blocks override); a missing file or key is an error
	static bool load(ArchiveFileSystem &fs, EconomySettings &out, std::string *error);
	static bool scan(const std::string &text, EconomySettings &out, std::string *error);
	void crc(StateHasher &h) const;
};

struct EconomyContext
{
	enum GameMode
	{
		MODE_SINGLE_PLAYER = 0,
		MODE_LAN = 1,
		MODE_SKIRMISH = 2,
		MODE_REPLAY = 3,
		MODE_SHELL = 4,
		MODE_INTERNET = 5
	};
	int gameMode = MODE_SKIRMISH;
	int gameKind = 3;
	bool livingWorld = false;
	bool gameInfoPresent = true;
	int lobbyCommandPointPercent = 100;
	bool scoring = true;
	void crc(StateHasher &h) const;
};
