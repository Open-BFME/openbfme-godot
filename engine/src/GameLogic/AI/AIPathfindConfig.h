// OpenBFME. GPL-3.0.
//
// The pathfinder's configuration from the game's INI files (lane PATH-1). The values are read from the files of the
// mounted install: a missing file or GameData key is an error (CLAUDE.md "no fallbacks"); an AIData field left out keeps
// retail's AIData constructor value, as in retail.
//
// lane AIDATA-1: the AIData fields (WadeWaterDepth, DeepWaterDepth, HordesWaitForHordes, MeleeApproachTolerance,
// MeleeApproachDist, CastleSiegeStandBackDistance, AttackUsesLineOfSight) come from the shared AIData table
// (GameLogicSettingsLoader::loadAIData: default\aidata.ini then aidata.ini, the retail parsers, the AIData constructor's
// value for a field left out); a map's map.ini / solo.ini AIData block reaches them through applyAIData.
// STOP S-160 (docs/STOPS.md): the GameData MaxCells* / PlanningModeEnabled values are still read by a text scan
// (block name, `Key = value` lines, ';' comments), like MapObjectGameData::load.

#pragma once

#include "GameLogic/AI/AIPathfind.h"

#include <string>

class ArchiveFileSystem;
struct GameLogicSettings;

namespace PathfindConfigLoader
{
// Fills every field of `out`; false + *error when a file or a required key is missing.
bool load(ArchiveFileSystem &fs, PathfindConfig &out, std::string *error);
// lane AIDATA-1: the AIData fields of `out` from the settings (after a map's AIData block: GameLogicSettingsLoader::applyMapAIData); `bfme1`: no deep water
void applyAIData(const GameLogicSettings &settings, PathfindConfig &out, bool bfme1);
// The scanning step on already-read text: `blockName` is the block to read, the callback receives each key / value.
// Exposed for the tests.
bool scanBlock(const std::string &text, const std::string &blockName, std::string *error,
	void (*onPair)(void *user, const std::string &key, const std::string &value), void *user);
} // namespace PathfindConfigLoader
