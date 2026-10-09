// OpenBFME. GPL-3.0.
//
// TheGlobalWeatherSystem. See GameLogic/GlobalWeatherSystem.h for the target facts. Lane SPELL-2.

#include "GameLogic/GlobalWeatherSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
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
	++m_weatherChanges; // RW 0x719DEE: the client's sky / rain / lighting change (S-922)
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

std::vector<std::string> GlobalWeatherSystem::stopLines()
{
	return { "[S-922] GlobalWeatherSystem (RW 0xDE772C): the weather state, the weather-based attribute modifier and their countdowns run; the client's "
			 "weather change (RW 0x719DEE: sky, rain, lighting) is counted, the burn decay value goes to an unidentified subsystem (RW 0xDE46A8 + 0x98, "
			 "kept here), and the weather at game start is taken as NONE (the map's setting is not traced)" };
}
