// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/Skirmish/SkirmishGameSetup.h.

#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"

#include <algorithm>
#include <cstdio>
#include <set>

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
	// lane RULES-1: RotWK's GameInfo::reset leaves the starting money unset (m_54 = -1, RW 0x801AAF): Player::init gives every player the InitialResources
	// rule instead (RW 0x6B0578); AptSkirmish::InitTheGame (RW 0x9288D0) copies the mode's default rules into the game when the profile has no kept game
	// (lane UI-6: restoreSaved takes the kept one's)
	m_info.startingCash = -1;
	m_info.rules = MpGameRules::defaults(rulesMode());
	m_info.legendaryHeroes = LegendaryHeroes::kOff;
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

bool SkirmishGameSetup::restoreSaved(const SkirmishGameInfo &saved, const std::string &preferredMap, std::uint32_t seed, std::string *error)
{
	// RW 0x9288D0 (see the header)
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	const std::string mapName = preferredMap.empty() ? saved.mapName : preferredMap;
	const MapCacheEntry *md = findMap(mapName);
	if (!md)
	{
		return fail("the kept game's map '" + mapName + "' is not in the map cache");
	}
	if (!saved.slots[0].isHuman())
	{
		return fail("the kept game's slot 0 is not a human");
	}
	SkirmishGameInfo info = m_info;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		SkirmishGameSlot slot = saved.slots[i];
		if (!slot.isHuman())
		{
			slot.name = stateText(slot.state); // GameSlot::setState gives a computer, open or closed slot its state's text
		}
		info.slots[i] = slot;
	}
	info.rules = saved.rules;
	info.seed = seed;
	info.mapName = md->name;
	info.mapCRC = md->fileCRC;
	info.mapSize = md->fileSize;
	info.mapMask = m_source.mapContentsMask(*md);
	m_info = info;
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
	if (!m_info.customHeroesAllowed())
	{
		if (error)
		{
			*error = "AllowCustomHeroes is off"; // lane RULES-1: the hero combo is disabled (RW 0x842A85)
		}
		return false;
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

void SkirmishGameSetup::setStrategicMode(bool strategic)
{
	if (strategic == m_strategic)
	{
		return;
	}
	m_strategic = strategic;
	m_info.rules = MpGameRules::defaults(rulesMode());
}

std::u16string SkirmishGameSetup::ruleName(int rule) const
{
	const char *tag = MpGameRules::ruleLabel(rule);
	return tag ? label(tag) : std::u16string(u"Invalid Rule"); // RW 0xC8292C
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::ruleComboEntries(const MpRuleCombo &combo) const
{
	// RW 0x986B1C: text.format(fetchFormat(choice.label), choice.value); the default index appends fetch("VALUE:Default"); item data = the value
	std::vector<SkirmishComboEntry> out;
	for (int j = 0; j < combo.count; ++j)
	{
		const MpRuleChoice &choice = combo.choices[j];
		std::u16string text = formatInt(label(choice.label), choice.value);
		if (j == combo.defaultIndex)
		{
			text += label("VALUE:Default");
		}
		out.push_back({ text, 0xFFFFFFFFu, choice.value });
	}
	return out;
}

bool SkirmishGameSetup::setRule(int rule, int value)
{
	if ((unsigned)rule >= (unsigned)NUM_GAME_RULES)
	{
		return false;
	}
	m_info.rules[(size_t)rule] = value;
	// RW 0x842A85: the hero combo of a slot is enabled only while rule 0 == 1; otherwise it is disabled and, when it had a choice, set to its first entry and
	// the hero handler runs (no hero)
	m_info.clearCreateAHeroesIfNotAllowed();
	return true;
}

void SkirmishGameSetup::resetRules()
{
	m_info.rules = MpGameRules::defaults(rulesMode());
	m_info.legendaryHeroes = LegendaryHeroes::kOff;
}

bool SkirmishGameSetup::setLegendaryHeroes(int factor)
{
	if (!LegendaryHeroes::valid(factor))
	{
		return false;
	}
	m_info.legendaryHeroes = factor;
	return true;
}

std::vector<SkirmishComboEntry> SkirmishGameSetup::legendaryHeroesEntries() const
{
	std::vector<SkirmishComboEntry> out;
	for (int factor : LegendaryHeroes::kChoices)
	{
		std::u16string text = LegendaryHeroes::choiceText(factor);
		if (factor == LegendaryHeroes::kOff)
		{
			text += label("VALUE:Default");
		}
		out.push_back({ text, 0xFFFFFFFFu, factor });
	}
	return out;
}

SkirmishGameSetup::StartCheck SkirmishGameSetup::checkStart(int flags, bool willTransfer) const
{
	// RW 0x8431B8 (see the header). The order of the tests and the texts are RotWK's; the owner's ready test (vslot 4) and the NAT test (vslot 0x5C) pass
	StartCheck r;
	// a refusal: its text goes to the owner's vslot 0x44 with each kind in order (RW 0x8406DB is kind 1; the jumps to RW 0x84382B add kind 2)
	auto refuse = [&](const char *labelName, const std::u16string &text, std::initializer_list<int> kinds) {
		r.ok = false;
		r.refusalLabel = labelName;
		r.refusal = text;
		for (int kind : kinds)
		{
			r.lines.push_back({ text, kind });
		}
		return r;
	};
	const bool wotr = m_strategic; // MpGameSetup + 0x7C == 1
	const MapCacheEntry *md = findMap(m_info.mapName);
	// RW 0x843245 .. 0x843276: the maximum (RW 0x9758E3: the map's player count, 0 without the map; above 8 the constant 8 at RW 0xC53F2C) and the minimum
	// (RW 0x97597F: 0 outside the War of the Ring; its scenario's minimum is not ported, stop S-3610)
	int maxPlayers = md ? (int)md->numPlayers : 0;
	if (maxPlayers >= 9)
	{
		maxPlayers = MAX_SLOTS;
	}
	const int minPlayers = 0;
	if (wotr)
	{
		r.stops.push_back("[S-3610] start check: the War of the Ring scenario's player limits (RW 0x9758E3 / 0x97597F) and faction test (GUI:DisabledFaction, RW 0x843447) are not "
						  "ported: the map's player count stands in and every faction passes");
	}
	if (maxPlayers < 1 || maxPlayers < minPlayers)
	{
		return refuse("", std::u16string(), {}); // RW 0x843263: no text, the start simply does not happen
	}
	if (!md)
	{
		return refuse("LAN:NoMapSelected", label("LAN:NoMapSelected"), { 1 }); // RW 0x8438D3 (unreachable: no map gives no maximum)
	}
	bool allHaveMap = true, allAccepted = true, allSpots = true;
	int numUsers = 0, numHumans = 0, numRandom = 0, numMissingClan = 0;
	std::set<int> teams;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = m_info.slots[i];
		if (slot.state == SLOT_OPEN)
		{
			continue; // RW 0x843305 .. 0x843315: outside the War of the Ring the slot's selection is refreshed (RW 0x840C59): the screen's
		}
		const bool observer = slot.playerTemplate == PLAYERTEMPLATE_OBSERVER; // RW 0x800A1B
		if (slot.isHuman())
		{
			if (!slot.hasMap && !willTransfer)
			{
				std::u16string line = label("GUI:PlayerNoMap"); // "%ls does not have the map %ls." (kind 2)
				const std::size_t a = line.find(u"%ls");
				if (a != std::u16string::npos)
				{
					line.replace(a, 3, slot.name);
				}
				const std::size_t b = line.find(u"%ls");
				if (b != std::u16string::npos)
				{
					line.replace(b, 3, mapDisplayName(*md, true));
				}
				r.lines.push_back({ line, 2 }); // RW 0x84338A: kind 2
				allHaveMap = false;
			}
			if (!slot.accepted && i != 0)
			{
				allAccepted = false; // slot 0's accepted flag is set before the loop (RW 0x843280 region; decomp: getSlot(0)->m_accepted = true)
			}
			if (!observer)
			{
				++numMissingClan; // RW 0x7846EE: the player's clan tag; OpenBFME players carry none (only the clan game rule reads it)
			}
		}
		if (slot.isOccupied() && !observer)
		{
			++numUsers;
			numHumans += slot.isHuman() ? 1 : 0;
			if (slot.teamNumber < 0)
			{
				++numRandom;
			}
			else
			{
				teams.insert(slot.teamNumber);
			}
			// RW 0x843447 .. 0x843495: the War of the Ring scenario's faction test (GUI:DisabledFaction) is not ported (S-3610)
			if (slot.startPos == -1)
			{
				allSpots = false;
			}
		}
	}
	r.numHumans = numHumans;
	const bool wotrEnemy = wotr && (flags & 0x100) != 0; // RW 0x8434B3: + 0x3A5 & 1
	if (((flags & 0x10) != 0 || wotrEnemy) && numHumans <= 1)
	{
		return refuse("GUI:NeedHumanPlayers", label("GUI:NeedHumanPlayers"), { 0 }); // RW 0x8434DE (kind 0)
	}
	if (!allHaveMap)
	{
		return refuse("GUI:CouldNotTransferMap", label("GUI:CouldNotTransferMap"), { 1 });
	}
	if (maxPlayers < numUsers)
	{
		return refuse("LAN:TooManyPlayers", formatInt(label("LAN:TooManyPlayers"), maxPlayers), { 1 });
	}
	if (numUsers < minPlayers)
	{
		return refuse("LAN:TooFewPlayers", formatInt(label("LAN:TooFewPlayers"), minPlayers), { 1 }); // RotWK's addition (RW 0x843575)
	}
	const int globalMinPlayers = 0; // GlobalData + 0xA44 (read RW 0x84359C): the command line's value (RW 0x7BA380), 0 by default (RW 0x642F14); OpenBFME has no such switch
	if (globalMinPlayers != 0 && numHumans == 0)
	{
		return refuse("GUI:NeedHumanPlayers", label("GUI:NeedHumanPlayers"), { 1 });
	}
	if (wotr && !allSpots)
	{
		return refuse("GUI:NeedStartSpots", label("GUI:NeedStartSpots"), { 1, 2 }); // RW 0x8435F9 (GUI:DisabledFaction before it, RW 0x8435D9: S-3610)
	}
	if (numUsers < globalMinPlayers)
	{
		return refuse("GUI:NeedHumanPlayers", label("GUI:NeedHumanPlayers"), { 1 });
	}
	const unsigned numTeams = (unsigned)teams.size() + (unsigned)numRandom; // RW 0x843658 / 0x8436A3: unsigned compares
	if (numTeams < (unsigned)globalMinPlayers)
	{
		return refuse("LAN:NeedMoreTeams", label("LAN:NeedMoreTeams"), { 1, 2 });
	}
	if (numTeams < 2)
	{
		if (wotrEnemy)
		{
			return refuse("GUI:NeedWOTREnemy", label("GUI:NeedWOTREnemy"), { 1, 2 });
		}
		r.lines.push_back({ label("GUI:SandboxMode"), 0 }); // RW 0x8436D8 (kind 0): "Entering sandbox mode..." and the start goes on
		r.sandbox = true;
	}
	if (m_info.rules[RULE_CLAN_GAME] == 1) // RW 0x84370A: the setup's + 0x160 (its rule block + 0x15C, + 4: rule 1, ClanGame)
	{
		if (numMissingClan >= 1)
		{
			return refuse("CLAN:ErrorMissingClanAffiliation", label("CLAN:ErrorMissingClanAffiliation"), { 1, 2 });
		}
		if (numRandom != 0 || teams.size() != 2)
		{
			return refuse("CLAN:ErrorOnlyTwoClans", label("CLAN:ErrorOnlyTwoClans"), { 1, 2 });
		}
		if ((unsigned)numUsers >= 5)
		{
			return refuse("CLAN:ErrorClanMaxFourPlayers", label("CLAN:ErrorClanMaxFourPlayers"), { 1, 2 });
		}
		if ((unsigned)numUsers <= 1)
		{
			return refuse("CLAN:ErrorClanMinTwoPlayers", label("CLAN:ErrorClanMinTwoPlayers"), { 1, 2 });
		}
	}
	if (!allAccepted && (flags & 0x20) != 0)
	{
		return refuse("GUI:NotifiedStartIntent", label("GUI:NotifiedStartIntent"), { 2 });
	}
	r.ok = true;
	return r;
}

bool SkirmishGameSetup::validateStart(std::u16string &title, std::u16string &text) const
{
	const StartCheck c = checkStart();
	if (c.ok)
	{
		return true;
	}
	title.clear();
	text.clear();
	for (const StartCheck::Line &line : c.lines)
	{
		if (line.kind == 1)
		{
			text = line.text;
		}
	}
	return false;
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
