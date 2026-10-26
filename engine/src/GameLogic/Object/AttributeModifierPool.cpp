// OpenBFME. GPL-3.0.
// See GameLogic/Object/AttributeModifierPool.h.

#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/System/ShroudManager.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

AttributeModifierPool::AttributeModifierPool(Thing *thing, const ModuleData *data)
	: BehaviorModule(thing, data)
	, m_store(TheAttributeModifierStore) // the world's store, taken where the object is made (see m_store)
{
}

const AttributeModifierStore *AttributeModifierPool::store() const
{
	return m_store ? m_store : TheAttributeModifierStore;
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
		" expiring adds, the delayed Upgrade grant (" + std::to_string(s.delayedUpgrades) + " not granted), the expiry's EndFX and the xfer (lane DECOMP-1: a SHROUD_CLEARING list marks the shroud record dirty, RW 0x68C213; AttributeModifierNugget's anti-category "
		"disabling RW 0x804FCC runs)" };
}

// RW 0x804D27 (the entry's list fetched by the caller)
bool AttributeModifierPool::suppressed(unsigned frame, const ModifierListTemplate &list, bool innate) const
{
	const int cat = list.m_category;
	if (!innate && cat >= 10 && cat <= 14)
	{
		return true;
	}
	return cat >= 0 && cat < 15 && frame <= m_categoryDisabled[(size_t)cat];
}

bool AttributeModifierPool::sum(int type, const char *name, float &out) const
{
	out = 0.0f;
	if (!store())
	{
		return false;
	}
	const unsigned frame = getObject()->logic().getFrame();
	bool found = false;
	for (const Entry &e : m_entries)
	{
		// lane HDPERF-1 (performance): the list is fetched once and a list without a modifier of `type` is passed over first (it adds nothing, whatever
		// its expiry or suppression: the same answer)
		const ModifierListTemplate *list = store()->get(e.index);
		if (!list || !list->mayHave(type) || frame >= e.expire || suppressed(frame, *list, true))
		{
			continue;
		}
		float v = 0.0f;
		if (list->value(type, name, v))
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
	if (!store())
	{
		return false;
	}
	const unsigned frame = getObject()->logic().getFrame();
	bool found = false;
	for (const Entry &e : m_entries)
	{
		// lane HDPERF-1 (performance): the list is fetched once and a list without a modifier of `type` is passed over first (it adds nothing, whatever
		// its expiry or suppression: the same answer)
		const ModifierListTemplate *list = store()->get(e.index);
		if (!list || !list->mayHave(type) || frame >= e.expire || suppressed(frame, *list, innate))
		{
			continue;
		}
		float v = 0.0f;
		if (list->value(type, name, v))
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

namespace
{
// lane SPELL-4: RW 0x804E32 (FX) / 0x804E8A (EndFX): with MultiLevelFX and the object's pool, the entry (category count - 1) of FX / FX2 / FX3, else FX.
// INFERENCE (S-633): a count above 3 takes FX3 (the binary's index there is a reused stack slot)
const std::string &pickModifierFX(const std::string (&fx)[3], bool multiLevel, int categoryCount)
{
	if (multiLevel)
	{
		int i = categoryCount - 1;
		if (i > 2)
		{
			i = 2;
		}
		if (i >= 0)
		{
			return fx[i];
		}
	}
	return fx[0];
}
// RW 0x4B1B5A (FXList::doFXObj(fx, object, null)): a client effect of the logic's FX log (not state)
void emitModifierFX(Object &obj, const std::string &fx)
{
	if (FXEventLog::isFXName(fx))
	{
		obj.logic().fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "AttributeModifier FX", obj.logic().getFrame(), fx, obj));
	}
}
} // namespace

void AttributeModifierPool::handDownTo(Object &obj) const
{
	const unsigned frame = getObject()->logic().getFrame();
	const std::vector<Entry> entries = m_entries; // the member's own pool is another module: the copy only guards against a hand-down to this object
	for (const Entry &e : entries)
	{
		// INFERENCE (S-633, lane BANNER-1 review r1): the port keeps an expired entry (the pool's expiry removal is not ported) where retail's would be gone; it is
		// not handed down (a non-positive remaining duration would give the list its own default duration again)
		if (e.expire <= frame)
		{
			continue;
		}
		obj.addAttributeModifier(e.name, (int)(e.expire - frame)); // RW 0x804F18 .. 0x804F27
	}
}

bool AttributeModifierPool::add(const std::string &listName, int duration)
{
	if (!store())
	{
		return false;
	}
	const int index = store()->findIndex(listName);
	const ModifierListTemplate *list = store()->get(index);
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
			const ModifierListTemplate *other = store()->get(e.index);
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
			// RW 0x805C8E .. 0x805CB9 (lane SPELL-4): the refresh plays the list's FX while the entry runs, its EndFX when the expiry is already past
			const int count = categoryCount(cat);
			emitModifierFX(obj, frame < e.expire ? pickModifierFX(list->m_fx, list->m_multiLevelFX, count)
												  : pickModifierFX(list->m_endFx, list->m_multiLevelFX, count));
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
	// RW 0x805D00 .. 0x805D27 (lane SPELL-4): a new entry that has not expired plays the list's FX on the object (before the category count grows).
	// INFERENCE (S-633): RW 0x70E013's pointer (+ 0x84) is taken as the object's drawable; the client skips an object without one (LiveFX), so the logic
	// emits it for every object
	if (frame < expire)
	{
		emitModifierFX(obj, pickModifierFX(list->m_fx, list->m_multiLevelFX, categoryCount(cat)));
	}
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

// ---- lane SAVE-4b: the snapshot (OpenBFME native, S-4190; retail's AttributeModifierPoolUpdate::xfer RW 0x805E89 (1, 3) is an attempt in the decomp, its
// fields not identified one by one): the entries in order (each list's store index and name), the wake frame and the category tables --------------------
void AttributeModifierPool::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	SnapshotXfer::each(x, m_entries, 9, [&](Entry &e) {
		x.xferInt(&e.index);
		x.xferAsciiString(&e.name);
		// the list is the store's: its index is the name's (RW 0x614470)
		const int index = store() ? store()->findIndex(e.name) : -1; // the pool's world store (m_store)
		if (x.isLoading() && (index < 0 || index != e.index))
		{
			throw XferException("an attribute modifier entry names the list '" + e.name + "' at index " + std::to_string(e.index) + ", which this game's data has "
				+ (index < 0 ? std::string("not") : "at " + std::to_string(index)));
		}
		x.xferUnsignedInt(&e.expire);
	});
	x.xferUnsignedInt(&m_nextWake);
	for (unsigned &f : m_categoryDisabled)
	{
		x.xferUnsignedInt(&f);
	}
	for (int &c : m_categoryCount)
	{
		x.xferInt(&c);
	}
}
