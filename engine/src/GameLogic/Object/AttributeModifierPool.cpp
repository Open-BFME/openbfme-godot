// OpenBFME. GPL-3.0.
// See GameLogic/Object/AttributeModifierPool.h.

#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/System/ShroudManager.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

AttributeModifierPool::AttributeModifierPool(Thing *thing, const ModuleData *data)
	: BehaviorModule(thing, data)
{
}

void AttributeModifierPool::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("AttributeModifierPoolUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<AttributeModifierPool>(thing, data);
	});
}

AttributeModifierPool::Stats &AttributeModifierPool::stats()
{
	static Stats s;
	return s;
}

std::vector<std::string> AttributeModifierPool::stopLines()
{
	const Stats &s = stats();
	return { "[S-633] AttributeModifierPoolUpdate (RW 0x8057B0): add / sum / product run; not ported: the expiry update that removes entries (HEALTH / "
		"ClearModelCondition reversal, EndFX, category counts; the queries skip an expired entry themselves) with " + std::to_string(s.expiringAdds) +
		" expiring adds, the delayed Upgrade grant (" + std::to_string(s.delayedUpgrades) + " not granted), the client FX (" + std::to_string(s.fxNotShown) +
		" not shown) and the xfer (lane DECOMP-1: a SHROUD_CLEARING list marks the shroud record dirty, RW 0x68C213; AttributeModifierNugget's anti-category "
		"disabling RW 0x804FCC runs)" };
}

bool AttributeModifierPool::suppressed(unsigned frame, const Entry &e, bool innate) const
{
	const ModifierListTemplate *list = TheAttributeModifierStore ? TheAttributeModifierStore->get(e.index) : nullptr;
	const int cat = list ? list->m_category : 0;
	if (!innate && cat >= 10 && cat <= 14)
	{
		return true;
	}
	return cat >= 0 && cat < 15 && frame <= m_categoryDisabled[(size_t)cat];
}

bool AttributeModifierPool::sum(int type, const char *name, float &out) const
{
	out = 0.0f;
	if (!TheAttributeModifierStore)
	{
		return false;
	}
	const unsigned frame = getObject()->logic().getFrame();
	bool found = false;
	for (const Entry &e : m_entries)
	{
		if (frame >= e.expire || suppressed(frame, e, true))
		{
			continue;
		}
		const ModifierListTemplate *list = TheAttributeModifierStore->get(e.index);
		float v = 0.0f;
		if (list && list->value(type, name, v))
		{
			out = SimMath::addf32(out, v);
			found = true;
		}
	}
	return found;
}

bool AttributeModifierPool::product(int type, const char *name, bool innate, float &out) const
{
	out = 1.0f;
	if (!TheAttributeModifierStore)
	{
		return false;
	}
	const unsigned frame = getObject()->logic().getFrame();
	bool found = false;
	for (const Entry &e : m_entries)
	{
		if (frame >= e.expire || suppressed(frame, e, innate))
		{
			continue;
		}
		const ModifierListTemplate *list = TheAttributeModifierStore->get(e.index);
		float v = 0.0f;
		if (list && list->value(type, name, v))
		{
			out = SimMath::mulf32(out, v);
			found = true;
		}
	}
	return found;
}

bool AttributeModifierPool::hasList(const std::string &listName) const
{
	for (const Entry &e : m_entries)
	{
		if (e.name == listName)
		{
			return true;
		}
	}
	return false;
}

void AttributeModifierPool::removeByName(const std::string &listName)
{
	for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
	{
		if (it->name == listName)
		{
			m_entries.erase(it);
			return;
		}
	}
}

bool AttributeModifierPool::add(const std::string &listName, int duration)
{
	if (!TheAttributeModifierStore)
	{
		return false;
	}
	const int index = TheAttributeModifierStore->findIndex(listName);
	const ModifierListTemplate *list = TheAttributeModifierStore->get(index);
	if (!list)
	{
		return false;
	}
	Object &obj = *getObject();
	const unsigned frame = obj.logic().getFrame();
	const int cat = list->m_category;
	if (list->m_ignoreIfAnticategoryActive && frame < m_categoryDisabled[(size_t)cat])
	{
		return false;
	}
	int d = duration < 0 ? (int)list->m_duration : duration;
	if (d < 0)
	{
		d = 0;
	}
	const unsigned expire = d != 0 ? (unsigned)d + frame : 0x3FFFFFFFu;
	if (list->m_replaceInCategoryIfLongest)
	{
		std::vector<std::string> shorter;
		for (const Entry &e : m_entries)
		{
			const ModifierListTemplate *other = TheAttributeModifierStore->get(e.index);
			if (e.index != index && other && other->m_category == cat)
			{
				if (e.expire >= expire)
				{
					return false;
				}
				shorter.push_back(other->m_name);
			}
		}
		for (const std::string &n : shorter)
		{
			removeByName(n);
		}
	}
	for (Entry &e : m_entries)
	{
		if (e.index == index)
		{
			e.expire = expire; // RW 0x805C73
			if (expire < m_nextWake)
			{
				m_nextWake = expire;
			}
			++stats().fxNotShown;
			return true;
		}
	}
	// a new entry: the model conditions (set, then clear: RW 0x5E3BA5, 0x5E3B79)
	const Object::ModelConditionBits none{};
	obj.clearAndSetModelConditionFlags(none, list->m_modelCondition);
	obj.clearAndSetModelConditionFlags(list->m_clearModelCondition, none);
	if (list->m_upgrade && list->m_upgrade->upgrade)
	{
		if (list->m_upgrade->delayFrames == 0)
		{
			obj.giveUpgrade(list->m_upgrade->upgrade); // RW 0x805C6C
		}
		else
		{
			++stats().delayedUpgrades;
		}
	}
	if (expire < m_nextWake)
	{
		m_nextWake = expire;
	}
	if (expire != 0x3FFFFFFFu)
	{
		++stats().expiringAdds;
	}
	++stats().fxNotShown;
	// HEALTH (RW 0x805D3A): the pool's HEALTH_MULT product (before this entry) scales it
	float health = 0.0f;
	if (list->value(ATTRIBUTE_HEALTH, nullptr, health) && health > 0.0f)
	{
		if (BodyModuleInterface *body = obj.getBodyModule())
		{
			float mult = 1.0f;
			const float max = body->getMaxHealth();
			if (product(ATTRIBUTE_HEALTH_MULT, nullptr, true, mult))
			{
				body->setMaxHealth(SimMath::addf32(max, SimMath::mulf32(mult, health)), 1); // fld mult; fmul health; faddp (PC24)
			}
			else
			{
				body->setMaxHealth(SimMath::addf32(max, health), 1);
			}
		}
	}
	// HEALTH_MULT (RW 0x805DB5)
	float healthMult = 0.0f;
	if (list->value(ATTRIBUTE_HEALTH_MULT, nullptr, healthMult) && healthMult > 0.0f)
	{
		if (BodyModuleInterface *body = obj.getBodyModule())
		{
			body->setMaxHealth(SimMath::mulf32(body->getMaxHealth(), healthMult), 1);
		}
	}
	Entry e;
	e.index = index;
	e.name = listName;
	e.expire = expire;
	m_entries.push_back(e);
	if (cat >= 0 && cat < 15)
	{
		++m_categoryCount[(size_t)cat];
	}
	// RW 0x805E2A .. 0x805E59 (lane DECOMP-1 r3): a list with a SHROUD_CLEARING value above 0 (type 0x14) marks the object's shroud record dirty, forced
	// (RW 0x68C213 -> 0xB4E2A0), so the wider look is taken at the next shroud update even for an object that does not move
	float shroudClearing = 0.0f;
	if (list->value(ATTRIBUTE_SHROUD_CLEARING, nullptr, shroudClearing) && shroudClearing > 0.0f)
	{
		if (ShroudManager *sm = obj.logic().shroud())
		{
			sm->markDirty(obj, true);
		}
	}
	return true;
}

void AttributeModifierPool::crc(StateHasher &hasher) const
{
	hasher.addU32((std::uint32_t)m_entries.size());
	for (const Entry &e : m_entries)
	{
		hasher.addI32(e.index);
		hasher.addString(e.name);
		hasher.addU32(e.expire);
	}
	hasher.addU32(m_nextWake);
	for (unsigned f : m_categoryDisabled)
	{
		hasher.addU32(f);
	}
	for (int c : m_categoryCount)
	{
		hasher.addI32(c);
	}
}
