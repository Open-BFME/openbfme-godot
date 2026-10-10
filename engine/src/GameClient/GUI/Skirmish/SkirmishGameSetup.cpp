// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/Skirmish/SkirmishGameSetup.h.

#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"

#include <algorithm>
#include <cstdio>

namespace
{
char16_t lowerU16(char16_t c)
{
	return (c >= u'A' && c <= u'Z') ? (char16_t)(c - u'A' + u'a') : c;
}
// rts::less_than_nocase<UnicodeString> (ZH Lib/BaseType.h): case-insensitive, shorter first on a common prefix
bool lessNoCase(const std::u16string &a, const std::u16string &b)
{
	const std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		const char16_t x = lowerU16(a[i]), y = lowerU16(b[i]);
		if (x != y)
		{
			return x < y;
		}
	}
	return a.size() < b.size();
}
std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = (char)(c - 'A' + 'a');
		}
	}
	return s;
}
// UnicodeString::format with one %d (the only format the lobby texts use: GUI:TooManyPlayers, GUI:StartingMoneyFormat)
std::u16string formatInt(const std::u16string &format, int value)
{
	std::u16string out;
	for (std::size_t i = 0; i < format.size(); ++i)
	{
		if (format[i] == u'%' && i + 1 < format.size() && format[i + 1] == u'd')
		{
			const std::string number = std::to_string(value);
			out.append(number.begin(), number.end());
			++i;
		}
		else
		{
			out.push_back(format[i]);
		}
	}
	return out;
}
} // namespace

SkirmishGameSetup::SkirmishGameSetup(const SkirmishSetupSource &source, const GameTextSource *text) : m_source(source), m_text(text) {}

const MapCacheEntry *SkirmishGameSetup::findMap(const std::string &mapName) const
{
	const std::string key = lowerAscii(mapName);
	for (const MapCacheEntry &m : m_source.maps())
	{
		if (m.name == key)
		{
			return &m;
		}
	}
	return nullptr;
}

std::u16string SkirmishGameSetup::stateText(SlotState state) const
{
	switch (state)
	{
		case SLOT_OPEN:
			return label("GUI:Open");
		case SLOT_EASY_AI:
			return label("GUI:EasyAI");
		case SLOT_MED_AI:
			return label("GUI:MediumAI");
		case SLOT_HARD_AI:
			return label("GUI:HardAI");
		case SLOT_BRUTAL_AI:
			return label("GUI:BrutalAI");
		case SLOT_CLOSED:
		default:
			return label("GUI:Closed");
	}
}

const SkirmishFaction *SkirmishGameSetup::faction(int playerTemplate) const
{
	if (playerTemplate < 0 || playerTemplate >= (int)m_source.factions().size())
	{
		return nullptr;
	}
	return &m_source.factions()[(std::size_t)playerTemplate];
}

// ZH GameSlot::setState (GameInfo.cpp:~255)
void SkirmishGameSetup::setSlotStateInternal(SkirmishGameSlot &slot, SlotState state, const std::u16string &name)
{
	const bool newIsAI = state >= SLOT_EASY_AI && state <= SLOT_BRUTAL_AI;
	if (!(slot.isAI() && newIsAI))
	{
		slot.color = -1;
		slot.startPos = -1;
		slot.playerTemplate = -1;
		slot.teamNumber = -1;
	}
	if (state == SLOT_PLAYER)
	{
		slot = SkirmishGameSlot(); // reset()
		slot.state = state;
		slot.name = name;
	}
	else
	{
		slot.state = state;
		slot.accepted = true;
		slot.hasMap = true;
		slot.name = stateText(state);
	}
}

bool SkirmishGameSetup::init(const std::u16string &userName, std::uint32_t seed, const std::string &mapName, std::string *error)
{
	m_error.clear();
	m_info = SkirmishGameInfo();
	// SkirmishGameOptionsMenuInit: init(); clearSlotList(); reset(): the last leaves every slot a reset GameSlot (closed, no name, not accepted); the
	// starting cash is the global default
	for (SkirmishGameSlot &s : m_info.slots)
	{
		s = SkirmishGameSlot();
	}
	m_info.startingCash = m_source.defaultStartingCash();
	m_info.seed = seed;
	setSlotStateInternal(m_info.slots[0], SLOT_PLAYER, userName);
	m_info.slots[0].accepted = true; // GameInfo::setSlot(0): setAccept() + setMapAvailability(true)
	// SkirmishScreenState: honors.getWins() <= 5 -> an easy AI in slot 1 (the honors file is not read [S-178]: a new profile has no wins)
	setSlotStateInternal(m_info.slots[1], SLOT_EASY_AI, std::u16string());
	std::string chosen = mapName.empty() ? defaultMapName() : mapName;
	if (chosen.empty() || !setMap(chosen))
	{
		m_error = "the map cache has no map '" + chosen + "'";
		if (error)
		{
			*error = m_error;
		}
		return false;
	}
	return true;
}

std::string SkirmishGameSetup::defaultMapName() const
{
	// ZH MapUtil.cpp getDefaultMap(TRUE): the first multiplayer map in the order of the cache's std::map (ascending key, the key is the path with
	// backslashes: SkirmishPreferences::getPreferredMap without a "Map" preference)
	std::string best, bestKey;
	for (const MapCacheEntry &m : m_source.maps())
	{
		if (!m.isMultiplayer)
		{
			continue;
		}
		std::string key = m.name;
		for (char &ch : key)
		{
			if (ch == '/')
			{
				ch = '\\';
			}
		}
		if (best.empty() || key < bestKey)
		{
			best = m.name;
			bestKey = key;
		}
	}
	return best;
}

bool SkirmishGameSetup::setMap(const std::string &mapName)
{
	const MapCacheEntry *md = findMap(mapName);
	if (!md)
	{
		m_error = "the map cache has no map '" + mapName + "'";
		return false;
	}
	m_info.mapName = md->name;
	m_info.mapCRC = md->fileCRC;
	m_info.mapSize = md->fileSize;
	m_info.mapMask = m_source.mapContentsMask(*md);
	for (SkirmishGameSlot &s : m_info.slots)
	{
		s.startPos = -1; // SkirmishMapSelectMenu.cpp:571
	}
	return true;
}

void SkirmishGameSetup::setPlayerName(const std::u16string &name)
{
	m_info.slots[0].name = name;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::playerEntries(int slot) const
{
	std::vector<SkirmishComboEntry> out;
	if (slotShowsName(slot))
	{
		out.push_back({ std::u16string(), 0xFFFFFFFFu, 0 }); // the local slot (lane MP-2: and a network game's human): one blank entry (BFME1 0x00527220)
		return out;
	}
	out.push_back({ label("GUI:Open"), 0xFFFFFFFFu, SLOT_OPEN });
	out.push_back({ label("GUI:Closed"), 0xFFFFFFFFu, SLOT_CLOSED });
	out.push_back({ label("GUI:EasyAI"), 0xFFFFFFFFu, SLOT_EASY_AI });
	out.push_back({ label("GUI:MediumAI"), 0xFFFFFFFFu, SLOT_MED_AI });
	out.push_back({ label("GUI:HardAI"), 0xFFFFFFFFu, SLOT_HARD_AI });
	out.push_back({ label("GUI:BrutalAI"), 0xFFFFFFFFu, SLOT_BRUTAL_AI });
	return out;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::templateEntries() const
{
	std::vector<SkirmishComboEntry> out;
	out.push_back({ label("GUI:Random"), 0xFFFFFFFFu, PLAYERTEMPLATE_RANDOM, std::string() });
	const std::vector<SkirmishFaction> &factions = m_source.factions();
	for (int c = 0; c < (int)factions.size(); ++c)
	{
		const SkirmishFaction &fac = factions[(std::size_t)c];
		if (!fac.playableSide || fac.isObserver)
		{
			continue;
		}
		out.push_back({ label(fac.displayLabel), 0xFFFFFFFFu, c, std::string() });
	}
	return out;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::colorEntries(int slot) const
{
	const std::vector<SkirmishColor> &colors = m_source.colors();
	std::vector<bool> available(colors.size(), true);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const int c = m_info.slots[i].color;
		if (i != slot && c >= 0 && c < (int)colors.size())
		{
			available[(std::size_t)c] = false;
		}
	}
	std::vector<SkirmishComboEntry> out;
	out.push_back({ label("GUI:???"), 0xFFFFFFFFu, -1, "AptRandomColor" });
	for (int c = 0; c < (int)colors.size(); ++c)
	{
		if (!available[(std::size_t)c] || (m_strategic && !colors[(std::size_t)c].availableInWotR))
		{
			continue;
		}
		out.push_back({ label(colors[(std::size_t)c].tooltipName), 0xFF000000u | colors[(std::size_t)c].rgb, c, "AptWhiteBox" });
	}
	return out;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::teamEntries() const
{
	std::vector<SkirmishComboEntry> out;
	out.push_back({ label("Team:0"), 0xFFFFFFFFu, -1 });
	for (int c = 0; c < MAX_SLOTS / 2; ++c)
	{
		out.push_back({ label("Team:" + std::to_string(c + 1)), 0xFFFFFFFFu, c });
	}
	return out;
}

namespace
{
std::string u16ToAscii(const std::u16string &s)
{
	std::string out;
	for (char16_t c : s)
	{
		out.push_back(c < 0x80 ? (char)c : '?');
	}
	return out;
}
// MapMetaData::m_fileName is the key with backslashes
std::string backslashName(const std::string &name)
{
	std::string out = name;
	for (char &c : out)
	{
		if (c == '/')
		{
			c = '\\';
		}
	}
	return out;
}
std::string mapStringFile(const MapCacheEntry &map)
{
	const std::string file = backslashName(map.name);
	const std::size_t slash = file.rfind('\\');
	return slash == std::string::npos ? std::string() : file.substr(0, slash + 1) + "map.str";
}
} // namespace

std::u16string SkirmishGameSetup::mapBaseDisplayName(const MapCacheEntry &map) const
{
	const std::string stringFile = mapStringFile(map);
	const bool dollar = !map.displayName.empty() && map.displayName[0] == u'$';
	const std::string labelText = u16ToAscii(dollar ? map.displayName.substr(1) : map.displayName);
	std::u16string text;
	if (m_text && !stringFile.empty() && m_text->fetchMapLabel(stringFile, labelText, text))
	{
		return text;
	}
	if (m_text && m_text->fetch(labelText, text))
	{
		return text;
	}
	if (!stringFile.empty() && dollar)
	{
		const std::string file = backslashName(map.name);
		std::u16string leaf = asciiToU16(file.substr(file.rfind('\\') + 1));
		if (leaf.size() >= 4 && leaf.compare(leaf.size() - 4, 4, u".map") == 0)
		{
			leaf.erase(leaf.size() - 4);
		}
		return leaf;
	}
	return map.displayName;
}

std::u16string SkirmishGameSetup::mapDisplayName(const MapCacheEntry &map, bool includePlayerCount) const
{
	std::u16string name = mapBaseDisplayName(map);
	if (includePlayerCount && map.numPlayers >= 2)
	{
		name += formatInt(u" (%d)", (int)map.numPlayers);
	}
	return name;
}

std::u16string SkirmishGameSetup::mapDescription(const MapCacheEntry &map) const
{
	const std::string stringFile = mapStringFile(map);
	const std::string labelText = u16ToAscii(map.description);
	std::u16string text;
	if (m_text && !stringFile.empty() && m_text->fetchMapLabel(stringFile, labelText, text))
	{
		return text;
	}
	if (m_text && m_text->fetch(labelText, text))
	{
		return text;
	}
	return map.description;
}

std::u16string SkirmishGameSetup::mapDescriptionFirstLine(const MapCacheEntry &map) const
{
	std::u16string d = mapDescription(map);
	const std::size_t nl = d.find(char16_t(10));
	return nl == std::u16string::npos ? d : d.substr(0, nl);
}

std::vector<int> SkirmishGameSetup::listedMaps() const
{
	// open-bfme-2 MapUtil.cpp populateMapListboxNoReset: for numPlayers = 0, 1, 2 ...: the maps with that player count, ordered by display name
	// (a std::set of rts::less_than_nocase<UnicodeString>: equal names collapse to one entry, the map found last for a name wins); a map is
	// listed when its key starts with the system map directory, its multiplayer flag matches (skirmish: true) and its display name is not
	// empty.  Patch 1.03's two broken Generals maps are named by the donor; they are not in RotWK's cache.
	const std::vector<MapCacheEntry> &maps = m_source.maps();
	std::vector<int> out;
	int maxPlayers = 0;
	for (const MapCacheEntry &m : maps)
	{
		maxPlayers = std::max(maxPlayers, (int)m.numPlayers);
	}
	for (int players = 0; players <= maxPlayers; ++players)
	{
		std::vector<std::pair<std::u16string, int>> named;
		for (int i = 0; i < (int)maps.size(); ++i)
		{
			const MapCacheEntry &m = maps[(std::size_t)i];
			if (m.numPlayers != players)
			{
				continue;
			}
			if (m.displayName.empty())
			{
				continue;
			}
			const std::u16string shown = mapDisplayName(m, true);
			bool replaced = false;
			for (auto &entry : named)
			{
				if (!lessNoCase(entry.first, shown) && !lessNoCase(shown, entry.first))
				{
					entry.second = i; // filenameMap[displayName] = it->first: the last map with that name
					replaced = true;
					break;
				}
			}
			if (!replaced)
			{
				named.emplace_back(shown, i);
			}
		}
		std::stable_sort(named.begin(), named.end(), [](const auto &a, const auto &b) { return lessNoCase(a.first, b.first); });
		for (const auto &entry : named)
		{
			const MapCacheEntry &m = maps[(std::size_t)entry.second];
			if (m.name.compare(0, 5, "maps/") == 0 && m.isMultiplayer && !m.displayName.empty())
			{
				out.push_back(entry.second);
			}
		}
	}
	return out;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::startingCashEntries() const
{
	std::vector<SkirmishComboEntry> out;
	const std::u16string format = label("GUI:StartingMoneyFormat");
	for (int amount : m_source.startingCashChoices())
	{
		out.push_back({ formatInt(format, amount), 0xFFFFFFFFu, amount });
	}
	return out;
}

bool SkirmishGameSetup::setSlotState(int slot, SlotState state, const std::u16string &title)
{
	if (slot <= 0 || slot >= MAX_SLOTS)
	{
		return false; // handlePlayerSelection: index == 0 || index >= MAX_SLOTS
	}
	setSlotStateInternal(m_info.slots[slot], state, title);
	return true;
}

bool SkirmishGameSetup::setSlotColor(int slot, int color)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return false;
	}
	SkirmishGameSlot &s = m_info.slots[slot];
	if (color == s.color)
	{
		return true;
	}
	if (color >= -1 && color < (int)m_source.colors().size())
	{
		if (color != -1)
		{
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				if (i != slot && m_info.slots[i].color == color)
				{
					return false;
				}
			}
		}
	}
	s.color = color;
	return true;
}

bool SkirmishGameSetup::setSlotTemplate(int slot, int playerTemplate)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return false;
	}
	m_info.slots[slot].playerTemplate = playerTemplate;
	return true;
}

bool SkirmishGameSetup::setSlotTeam(int slot, int team)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return false;
	}
	m_info.slots[slot].teamNumber = team;
	return true;
}

bool SkirmishGameSetup::setSlotCreateAHero(int slot, const CreateAHeroHero *hero, std::string *error)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return false;
	}
	if (!hero)
	{
		m_info.slots[slot].clearCreateAHero();
		return true;
	}
	return m_info.slots[slot].setCreateAHero(*hero, error);
}

bool SkirmishGameSetup::setSlotStartPos(int slot, int position)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return false;
	}
	SkirmishGameSlot &s = m_info.slots[slot];
	if (position == s.startPos)
	{
		return true;
	}
	if (position < 0)
	{
		s.startPos = position;
		return true;
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (i != slot && m_info.slots[i].startPos == position)
		{
			return false;
		}
	}
	s.startPos = position;
	return true;
}

bool SkirmishGameSetup::setStartingCash(int cash)
{
	m_info.startingCash = cash;
	return true;
}

bool SkirmishGameSetup::validateStart(std::u16string &title, std::u16string &text) const
{
	// startPressed (ZH SkirmishGameOptionsMenu.cpp:506): the donor falls through to a dereference of the end iterator after the unknown-map
	// message box; here the refusal ends the check.
	const MapCacheEntry *md = findMap(m_info.mapName);
	if (!md)
	{
		title = label("GUI:ErrorStartingGame");
		text = label("GUI:CantFindMap");
		return false;
	}
	if (m_info.numPlayers() > md->numPlayers)
	{
		title = label("GUI:ErrorStartingGame");
		text = formatInt(label("GUI:TooManyPlayers"), (int)md->numPlayers);
		return false;
	}
	return true;
}

NewGameMessage SkirmishGameSetup::makeNewGameMessage()
{
	// GameInfo::startGame: closeOpenSlots() (every slot that is not occupied becomes a fresh closed slot), m_inProgress = true
	for (SkirmishGameSlot &s : m_info.slots)
	{
		if (!s.isOccupied())
		{
			s = SkirmishGameSlot();
			setSlotStateInternal(s, SLOT_CLOSED, std::u16string());
		}
	}
	m_info.inProgress = true;
	NewGameMessage m;
	const MapCacheEntry *md = findMap(m_info.mapName);
	// isSkirmish = md->m_isMultiplayer ("we can now select solo campaign maps in Skirmish")
	m.mode = (md && !md->isMultiplayer) ? NewGameMode::SinglePlayer : NewGameMode::Skirmish;
	m.difficulty = kDifficultyNormal;
	m.rankPoints = 0;
	m.game = m_info;
	return m;
}
