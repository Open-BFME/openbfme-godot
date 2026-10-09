// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h.

#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"

#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <algorithm>

bool WorldSkirmishSetupSource::load(RetailObjectWorld &world, const GameLogicSettings &settings, ArchiveFileSystem &fs, std::string *error)
{
	m_fs = &fs;
	m_factions.clear();
	m_colors.clear();
	m_cashChoices.clear();
	const PlayerTemplateStore &store = world.playerTemplates();
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = store.getNthPlayerTemplate(i);
		SkirmishFaction f;
		f.templateName = pt->getName();
		f.side = pt->m_side;
		f.displayLabel = pt->m_displayName;
		f.startingBuilding = pt->hasStartingBuilding() ? pt->m_startingBuilding : std::string();
		f.buildableHeroesMP = pt->m_buildableHeroesMP;
		f.playableSide = pt->m_playableSide;
		f.isObserver = pt->m_isObserver;
		const RGBColor &c = pt->m_preferredColor;
		f.preferredColor = SimMath::packRgb8(c.red, c.green, c.blue);
		m_factions.push_back(f);
	}
	for (const GameLogicSettings::MultiplayerColorDef &c : settings.multiplayerColors)
	{
		SkirmishColor sc;
		sc.name = c.name;
		sc.tooltipName = c.tooltipName;
		sc.rgb = c.rgb;
		sc.availableInWotR = c.availableInWotR;
		m_colors.push_back(sc);
	}
	if (m_colors.empty())
	{
		if (error)
		{
			*error = "multiplayer.ini defines no MultiplayerColor";
		}
		return false;
	}
	for (unsigned credits : settings.initialCredits)
	{
		m_cashChoices.push_back((int)credits);
	}
	std::sort(m_cashChoices.begin(), m_cashChoices.end());
	m_defaultStartingCash = (int)settings.defaultStartingCash;
	return IniSkirmishSetupSource::loadMapCache(fs, m_maps, error);
}

int WorldSkirmishSetupSource::mapContentsMask(const MapCacheEntry &map) const
{
	return IniSkirmishSetupSource::mapContentsMaskIn(m_fs, map);
}

bool WorldSkirmishSetupSource::fileExists(const std::string &path) const
{
	return m_fs && m_fs->doesFileExist(path);
}
