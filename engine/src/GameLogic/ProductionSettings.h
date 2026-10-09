// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ProductionSettings (lane PROD-1): the GameData values the build time arithmetic reads (RW 0x73C39E ThingTemplate::calcTimeToBuild), read from the
// mounted data\ini\gamedata.ini by name like GameLogicSettingsLoader does (the GameData block has no field-table parser yet).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; GlobalData = RW 0xDE4364): MinLowEnergyProductionSpeed +0xA74, MaxLowEnergyProductionSpeed +0xA78,
// LowEnergyPenaltyModifier +0xA7C, MultipleFactory +0xA80 (the build time of the second and later factories of a kind), MultiPlayUnitSpeedMult
// +0xEC4 + 0xF0 + 4 * (n - 1) and MultiPlayBuildingSpeedMult +0xEC4 + 0x140 + 4 * (n - 1) (`MP1:1.0 MP2:1.0 ...`, n = the number of live
// non-observer players, an index outside [0, 20) is 1.0).
// Command points (RW 0x6A7C86 CommandPoints::init, skirmish mode): with n live players the pair of that side for MP{n} (n >= 8: MP8, n <= 2: MP2) gives the
// base and the cap; the cap is scaled by the game setup's percentage (100 by default). Both pairs come from gamedata.ini.
// A missing key is an error (PLAN rule 10): `loaded` stays false and the arithmetic refuses to run.

#pragma once

#include <string>

class ArchiveFileSystem;

struct ProductionSettings
{
	bool loaded = false;
	float minLowEnergyProductionSpeed = 0.0f;
	float maxLowEnergyProductionSpeed = 0.0f;
	float lowEnergyPenaltyModifier = 0.0f;
	float multipleFactory = 0.0f;
	float multiPlayUnitSpeedMult[8] = {};
	float multiPlayBuildingSpeedMult[8] = {};
	int multiPlayEntries = 0; ///< how many MPn entries the INI lists (8 in retail)
	// (the command point pairs GameData {Good|Evil}CommandPointsMPn belong to the economy: GameLogic/EconomySettings.h)

	// the GameData block of `text`; false + *error when a key is missing or malformed
	static bool scan(const std::string &text, ProductionSettings &out, std::string *error);
	static bool load(ArchiveFileSystem &fs, ProductionSettings &out, std::string *error);
};
