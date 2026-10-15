// OpenBFME. GPL-3.0.
// See BuffDraw.h (lane SPELL-4). Client-only.

#include "GameClient/BuffDraw.h"

#include <iterator>

void ClientBuffs::updateFade(int &state, float &opacity)
{
	// RW 0x4D1417
	switch (state)
	{
	case FADE_IN:
		opacity = opacity + 0.083333336f;
		if (opacity >= 1.0f)
		{
			state = ON;
			opacity = 1.0f;
		}
		return;
	case ON:
		opacity = 1.0f;
		return;
	case FADE_OUT:
		opacity = opacity - 0.083333336f;
		if (opacity <= 0.0f)
		{
			state = GONE;
			opacity = 0.0f;
		}
		return;
	case GONE:
		opacity = 0.0f;
		return;
	default:
		return;
	}
}

void ClientBuffs::release(std::map<std::pair<ObjectID, int>, Buff>::iterator it)
{
	auto t = m_perTemplate.find(it->second.thing);
	if (t != m_perTemplate.end() && t->second > 0)
	{
		--t->second;
	}
	m_buffs.erase(it);
}

bool ClientBuffs::turnOn(ObjectID object, int type, const std::string &thing, const std::string &model, unsigned lifeFrames, unsigned logicFrame)
{
	// BuffManager::TurnOnBuff (RW 0x764483): types 1 .. 8; MakeNormalBuff (RW 0x7641A2)
	if (type >= MAX_TYPES || type < 1 || thing.empty() || model.empty())
	{
		return false;
	}
	const auto key = std::make_pair(object, type);
	auto it = m_buffs.find(key);
	if (it != m_buffs.end() && it->second.thing != thing)
	{
		release(it); // another template: the old TBuff is released, a new one made
		it = m_buffs.end();
	}
	if (it == m_buffs.end())
	{
		size_t &n = m_perTemplate[thing];
		if (n >= MAX_PER_TEMPLATE)
		{
			return false; // W3DBuffBuffer::addBuff RW 0x4D33E9: the type's set holds 1000 (the instance stays inactive)
		}
		++n;
		Buff b;
		b.object = object;
		b.type = type;
		b.thing = thing;
		b.model = model;
		b.serial = ++m_serial;
		it = m_buffs.emplace(key, b).first;
	}
	else if (it->second.state == FADE_OUT)
	{
		it->second.state = ON;
		it->second.opacity = 1.0f;
	}
	it->second.active = true;
	it->second.expireFrame = logicFrame + lifeFrames;
	return true;
}

void ClientBuffs::turnOff(ObjectID object, int type, bool immediate)
{
	// BuffManager::TurnOffBuff -> BuffInstance::TurnOff
	auto it = m_buffs.find(std::make_pair(object, type));
	if (it == m_buffs.end())
	{
		return;
	}
	if (immediate)
	{
		release(it);
		return;
	}
	if (it->second.active)
	{
		it->second.state = FADE_OUT;
		it->second.active = false;
	}
}

void ClientBuffs::removeObject(ObjectID object)
{
	for (auto it = m_buffs.lower_bound(std::make_pair(object, 0)); it != m_buffs.end() && it->first.first == object;)
	{
		auto next = std::next(it);
		release(it);
		it = next;
	}
}

void ClientBuffs::update(unsigned logicFrame)
{
	for (auto it = m_buffs.begin(); it != m_buffs.end();)
	{
		Buff &b = it->second;
		if (b.active && logicFrame >= b.expireFrame) // S-4642: the expiry
		{
			b.state = FADE_OUT;
			b.active = false;
		}
		updateFade(b.state, b.opacity);
		auto next = std::next(it);
		if (b.state == GONE)
		{
			release(it);
		}
		it = next;
	}
}

void ClientBuffs::advance(double seconds, unsigned logicFrame)
{
	m_clock += seconds;
	int steps = 0;
	while (m_clock >= 1.0 / 30.0 && steps < 8)
	{
		m_clock -= 1.0 / 30.0;
		update(logicFrame);
		++steps;
	}
	if (steps >= 8)
	{
		m_clock = 0.0; // a stall: no catching up
	}
}
