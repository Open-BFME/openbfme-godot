// OpenBFME. GPL-3.0.
//
// The SkirmishSetupSource of the real game (lane START-1): what the lobby lists comes from the SAME stores the logic starts the game from, so a
// slot's faction index is an index into the logic's PlayerTemplateStore and its colour index an index into the logic's multiplayer colour list.
//   * factions()  the RetailObjectWorld's PlayerTemplateStore in store order (RW store + 0xC); a slot's `playerTemplate` is that index (ZH GameSlot::
//                 m_playerTemplate, ThePlayerTemplateStore->getNthPlayerTemplate)
//   * colors()    GameLogicSettings::multiplayerColors (the `MultiplayerColor` blocks of multiplayer.ini, in file order)
//   * the starting money choices and DefaultStartingCash from GameLogicSettings; the map list from maps\mapcache.ini.
// It replaces IniSkirmishSetupSource's name-level scans (stop S-178 B) for the game: the scans stay for the lobby tests that have no object world.

#pragma once

#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

class ArchiveFileSystem;
class RetailObjectWorld;
struct GameLogicSettings;

class WorldSkirmishSetupSource : public SkirmishSetupSource
{
public:
	// `world` must be loaded; `settings` is GameLogicSettingsLoader::load's result. False + *error when the map cache cannot be read.
	bool load(RetailObjectWorld &world, const GameLogicSettings &settings, ArchiveFileSystem &fs, std::string *error);

	const std::vector<SkirmishFaction> &factions() const override { return m_factions; }
	const std::vector<SkirmishColor> &colors() const override { return m_colors; }
	const std::vector<MapCacheEntry> &maps() const override { return m_maps; }
	int defaultStartingCash() const override { return m_defaultStartingCash; }
	const std::vector<int> &startingCashChoices() const override { return m_cashChoices; }
	int mapContentsMask(const MapCacheEntry &map) const override;
	bool fileExists(const std::string &path) const override;
	bool hasFileSystem() const override { return m_fs != nullptr; }

private:
	ArchiveFileSystem *m_fs = nullptr;
	std::vector<SkirmishFaction> m_factions;
	std::vector<SkirmishColor> m_colors;
	std::vector<MapCacheEntry> m_maps;
	std::vector<int> m_cashChoices;
	int m_defaultStartingCash = 0;
};
