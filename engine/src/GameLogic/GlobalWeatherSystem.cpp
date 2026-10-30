// OpenBFME. GPL-3.0.
//
// TheGlobalWeatherSystem. See GameLogic/GlobalWeatherSystem.h for the target facts. Lane SPELL-2.

#include "GameLogic/GlobalWeatherSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/SnapshotXferUtil.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"

void GlobalWeatherSystem::setWeatherModifier(int kind, std::shared_ptr<const ObjectFilter> filter, const Player *player, const std::string &listName,
	unsigned antiMask, unsigned frames)
{
	clear(); // RW 0x71A144
	m_kind = kind;
	m_filter = std::move(filter);
	m_listName = listName;
	m_antiMask = antiMask;
	m_playerMask = player ? (1u << ((unsigned)player->getPlayerIndex() & 31u)) : 0u; // RW 0x71A16A: 1 << (Player + 0x54 & 31)
	m_modifierFrames = frames;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject()) // RW 0x71A182 .. 0x71A1A0
	{
		apply(*o, false);
	}
}

void GlobalWeatherSystem::clear()
{
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		apply(*o, true);
	}
	m_kind = AFFECT_NONE;
	m_listName.clear();
	m_antiMask = 0;
	m_playerMask = 0;
	m_modifierFrames = 0;
}

void GlobalWeatherSystem::setWeather(int weather, unsigned burnDecay, unsigned frames)
{
	if (m_weather == weather)
	{
		return;
	}
	m_weather = weather;
	m_weatherFrames = frames;
	m_burnDecay = burnDecay;
	++m_weatherChanges; // RW 0x719DEE: the weather sound (the client follows the snapshot's weather: WeatherClient, S-4892)
}

void GlobalWeatherSystem::update()
{
	const unsigned modifierWas = m_modifierFrames--;
	if (modifierWas != 0 && m_modifierFrames == 0)
	{
		clear();
	}
	const unsigned weatherWas = m_weatherFrames--;
	if (weatherWas != 0 && m_weatherFrames == 0)
	{
		setWeather(WEATHER_NONE, 0, 0);
	}
}

const Player *GlobalWeatherSystem::maskPlayer() const
{
	if (m_playerMask == 0)
	{
		return nullptr;
	}
	const PlayerList &players = m_logic.players();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		const Player *p = players.getNthPlayer(i);
		if (p && (m_playerMask & (1u << ((unsigned)p->getPlayerIndex() & 31u))) != 0)
		{
			return p;
		}
	}
	return nullptr;
}

void GlobalWeatherSystem::apply(Object &obj, bool remove)
{
	if (m_kind == AFFECT_NONE)
	{
		return;
	}
	if (m_kind == AFFECT_EVIL || m_kind == AFFECT_GOOD)
	{
		const Player *owner = obj.getControllingPlayer();
		if (owner)
		{
			const PlayerTemplate *pt = owner->getPlayerTemplate();
			const bool evil = pt && pt->m_evil; // PlayerTemplate + 0x1BC (a player without a template reads false)
			if (m_kind == AFFECT_EVIL ? !evil : evil)
			{
				return;
			}
		}
	}
	// RW 0x7640C1: a filter that was never set has no table entry; the weather-based powers of retail always name one. A null filter here allows
	// nothing (S-922 counts the power that would reach it, see SpecialPowerModule)
	if (!m_filter || !ObjectFilterMatch::allows(m_logic, *m_filter, obj, maskPlayer()))
	{
		return;
	}
	if (!m_listName.empty())
	{
		if (!remove)
		{
			obj.addAttributeModifier(m_listName, -1); // RW 0x68F1A8
		}
		else
		{
			obj.removeAttributeModifier(m_listName); // RW 0x68F259
		}
	}
	if (m_antiMask != 0)
	{
		if (AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(obj.findModule("AttributeModifierPoolUpdate"))) // RW 0x68C4A6
		{
			pool->disableCategories(m_antiMask, remove ? 0u : 999999999u); // RW 0x804FCC
		}
	}
}

void GlobalWeatherSystem::reset()
{
	m_weather = WEATHER_NONE;
	m_kind = AFFECT_NONE;
	m_filter.reset();
	m_listName.clear();
	m_playerMask = 0;
	m_weatherFrames = 0;
	m_modifierFrames = 0;
	m_antiMask = 0;
	m_burnDecay = 0;
	m_weatherChanges = 0;
}

void GlobalWeatherSystem::crc(StateHasher &h) const
{
	h.addI32(m_weather);
	h.addI32(m_kind);
	h.addBool(m_filter != nullptr);
	ObjectFilterMatch::crc(h, m_filter.get());
	h.addString(m_listName);
	h.addU32(m_playerMask);
	h.addU32(m_weatherFrames);
	h.addU32(m_modifierFrames);
	h.addU32(m_antiMask);
	h.addU32(m_burnDecay);
}

// lane SAVE-4d. TARGET FACTS (RW 0x71A2E3, tier A, this = TheGlobalWeatherSystem + 0xC): version (1, 4); + 0x10 the weather (XferGlobalWeatherType
// RW 0x70777F, 4 bytes); + 0x14 through TheAudio's vslot 0x160 (an audio handle of the client's weather sound: not held here, not written, S-4251);
// + 0x18 the affect kind (RW 0x707797, 4 bytes); + 0x1C the ObjectFilter (ObjectFilter::DoXfer RW 0x763EDF); + 0x20 the ModifierList name (AsciiString);
// a 4-byte AttributeModifierCategory that saving always writes as 0 (a local zeroed at RW 0x71A340: the version 1 field, which version 1 loads turn into
// the anti mask's one bit); version >= 2: + 0x24 the player mask; version >= 3: + 0x28 the weather frames, + 0x2C the modifier frames (u32); version
// >= 4: + 0x58 the AntiCategory mask as BitFlags<15>::xfer (RW 0x71A1A7: version (1, 1), the count of set bits, each set bit's name in bit order).
// OpenBFME (S-4251): the filter is the port's own ObjectFilter layout (ObjectFilterMatch::xfer, S-4121), and the burn decay (RW 0xDE46A8 + 0x98, another
// subsystem's field in retail, kept here and hashed) follows with its own version. The weather change count is the client's (not saved).
// The load (SAVE-4a review r1 rules, SAVES.md section 8): every field goes into a temporary, the closed sets are checked (the weather 0 .. 4: NONE ..
// SUNNY, RW 0xDB0074, MOUNTED = "no change" is never stored; the affect kind 0 .. 3; the player mask 0 or the one bit of a player of this game; the
// ModifierList name empty or a list of TheAttributeModifierStore), then the system takes them all at once.
void GlobalWeatherSystem::xfer(Xfer &x)
{
	const bool loading = x.isLoading();
	XferVersion v{ 1, 4 };
	x.xferVersion(v);
	std::int32_t weather = loading ? 0 : m_weather, kind = loading ? 0 : m_kind;
	std::shared_ptr<const ObjectFilter> filter = loading ? nullptr : m_filter;
	std::string listName = loading ? std::string() : m_listName;
	std::uint32_t playerMask = loading ? 0 : m_playerMask, weatherFrames = loading ? 0 : m_weatherFrames, modifierFrames = loading ? 0 : m_modifierFrames;
	std::uint32_t antiMask = loading ? 0 : m_antiMask;
	x.xferInt(&weather);
	x.xferInt(&kind);
	ObjectFilterMatch::xfer(x, filter);
	x.xferAsciiString(&listName);
	std::int32_t oldCategory = 0;
	x.xferInt(&oldCategory);
	if (v.current >= 2)
	{
		x.xferUnsignedInt(&playerMask);
	}
	if (v.current >= 3)
	{
		x.xferUnsignedInt(&weatherFrames);
		x.xferUnsignedInt(&modifierFrames);
	}
	if (v.current >= 4)
	{
		SnapshotXferUtil::xferBitFlagNames(x, antiMask, TheAntiCategoryNames, 15, "AntiCategory");
	}
	else if (loading)
	{
		if (oldCategory < 0 || oldCategory >= 15)
		{
			throw XferException("weather: an AntiCategory of " + std::to_string(oldCategory));
		}
		antiMask = oldCategory != 0 ? 1u << (std::uint32_t)oldCategory : 0u; // RW 0x71A38E .. 0x71A3B5
	}
	XferVersion own{ 1, 1 };
	x.xferVersion(own);
	std::uint32_t burnDecay = loading ? 0 : m_burnDecay;
	x.xferUnsignedInt(&burnDecay);
	if (!loading)
	{
		return;
	}
	if (weather < WEATHER_NONE || weather >= WEATHER_NO_CHANGE)
	{
		throw XferException("weather: a weather of " + std::to_string(weather));
	}
	if (kind < AFFECT_NONE || kind > AFFECT_GOOD)
	{
		throw XferException("weather: an affect kind of " + std::to_string(kind));
	}
	if (playerMask != 0)
	{
		bool found = false;
		const PlayerList &players = m_logic.players();
		for (int i = 0; i < players.getPlayerCount(); ++i)
		{
			const Player *p = players.getNthPlayer(i);
			found = found || (p && playerMask == (1u << ((unsigned)p->getPlayerIndex() & 31u)));
		}
		if (!found)
		{
			throw XferException("weather: a player mask of " + std::to_string(playerMask) + ", which is not one player of this game");
		}
	}
	if (!listName.empty() && (!TheAttributeModifierStore || !TheAttributeModifierStore->find(listName)))
	{
		throw XferException("weather: the ModifierList '" + listName + "' is not in this game's data");
	}
	m_weather = weather;
	m_kind = kind;
	m_filter = std::move(filter);
	m_listName = listName;
	m_playerMask = playerMask;
	m_weatherFrames = weatherFrames;
	m_modifierFrames = modifierFrames;
	m_antiMask = antiMask;
	m_burnDecay = burnDecay;
}

std::vector<std::string> GlobalWeatherSystem::stopLines()
{
	return { "[S-922] GlobalWeatherSystem (RW 0xDE772C): the weather state, the weather-based attribute modifier and their countdowns run; the burn "
			 "decay value goes to an unidentified subsystem (RW 0xDE46A8 + 0x98, kept here); lane WEATHER-1 resolved the rest: RW 0x719DEE is the "
			 "weather sound (played by the client from the presented weather, S-4892) and the weather at game start is NONE (ctor RW 0x719EBE, reset "
			 "RW 0x719FDB; setWeather is called only by the power trigger and the update)" };
}
