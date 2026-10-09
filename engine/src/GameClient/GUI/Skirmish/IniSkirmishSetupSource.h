// OpenBFME. GPL-3.0.
//
// The interim SkirmishSetupSource: it reads what the lobby lists straight from the mounted archives with name-level scans.
//   * data\ini\playertemplate.ini: `PlayerTemplate <Name>` blocks (Side, PlayableSide, IsObserver, StartingBuilding, DisplayName, PreferredColor,
//     BuildableHeroesMP).  LOGIC-1's PlayerTemplateStore is the real parse (full field table, retail-exact acceptance); it replaces this scan.
//   * data\ini\multiplayer.ini: `MultiplayerSettings` InitialCredits* and the `MultiplayerColor` blocks (RGBColor, TooltipName, AvailableInWotR).
//   * data\ini\gamedata.ini: GameData `DefaultStartingCash`.
//   * maps\mapcache.ini through MapCache::parse (the 122 maps of RotWK 2.01).
// Stop S-178 records what the scans do not check (unknown keys are skipped without validation, block keywords are matched by name).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

class IniSkirmishSetupSource : public SkirmishSetupSource
{
public:
	// Loads every file; false and `error` on a missing file or a parse error (never a silent default).
	bool load(ArchiveFileSystem &fs, std::string *error);

	const std::vector<SkirmishFaction> &factions() const override { return m_factions; }
	const std::vector<SkirmishColor> &colors() const override { return m_colors; }
	const std::vector<MapCacheEntry> &maps() const override { return m_maps; }
	int defaultStartingCash() const override { return m_defaultStartingCash; }
	const std::vector<int> &startingCashChoices() const override { return m_cashChoices; }
	int mapContentsMask(const MapCacheEntry &map) const override;
	bool fileExists(const std::string &path) const override;
	bool hasFileSystem() const override { return m_fs != nullptr; }

	// Shared with WorldSkirmishSetupSource: the map cache read and the files-next-to-the-map mask.
	static bool loadMapCache(ArchiveFileSystem &fs, std::vector<MapCacheEntry> &out, std::string *error);
	static int mapContentsMaskIn(ArchiveFileSystem *fs, const MapCacheEntry &map);

	// The scans on text (tests, mods).
	static bool scanPlayerTemplates(const std::string &text, std::vector<SkirmishFaction> &out, std::string *error);
	static bool scanMultiplayer(const std::string &text, std::vector<SkirmishColor> &colors, std::vector<int> &cashChoices, std::string *error);
	static bool scanDefaultStartingCash(const std::string &text, int &out, std::string *error);

private:
	ArchiveFileSystem *m_fs = nullptr;
	std::vector<SkirmishFaction> m_factions;
	std::vector<SkirmishColor> m_colors;
	std::vector<MapCacheEntry> m_maps;
	std::vector<int> m_cashChoices;
	int m_defaultStartingCash = 0;
};
