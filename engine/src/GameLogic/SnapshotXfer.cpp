// OpenBFME. GPL-3.0.
// See GameLogic/SnapshotXfer.h (lane SAVE-4b).

#include "Common/Thing/KindOfTokens.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstdio>

#include "Common/SpecialPower.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

namespace SnapshotXfer
{
void coord3D(Xfer &x, Coord3D &c)
{
	x.xferReal(&c.x);
	x.xferReal(&c.y);
	x.xferReal(&c.z);
}

void coord2D(Xfer &x, Coord2D &c)
{
	x.xferReal(&c.x);
	x.xferReal(&c.y);
}

void icoord2D(Xfer &x, ICoord2D &c)
{
	x.xferInt(&c.x);
	x.xferInt(&c.y);
}

// OpenBFME native (retail's DamageInfo::xfer is not one transfer of these fields: RotWK keeps the input at D + 0x08 .. + 0x68 as listed in Damage.h)
void damageInput(Xfer &x, DamageInfoInput &in)
{
	version(x, 1, 1);
	x.xferUnsignedInt(&in.m_sourceID);
	x.xferUnsignedInt(&in.m_sourcePlayerMask);
	// the enums, each from its binary name list (RW 0xDA15A8 damage types, 0xDA1510 damage FX types, the damage sub types, 0xDA1630 death types)
	enumIn(x, in.m_damageType, 0, DAMAGE_NUM_TYPES - 1, "a damage record's damage type");
	enumIn(x, in.m_damageFXOverride, 0, nameCount(TheDamageFXTypeNames) - 1, "a damage record's damage FX type");
	enumIn(x, in.m_damageSubType, 0, nameCount(TheDamageSubTypeNames) - 1, "a damage record's damage sub type");
	enumIn(x, in.m_deathType, 0, DEATH_NUM_TYPES - 1, "a damage record's death type");
	x.xferReal(&in.m_amount);
	x.xferBool(&in.m_kill);
	x.xferBool(&in.m_shouldPlayUnderAttackEva);
	x.xferReal(&in.m_delay);
	enumIn(x, in.m_fxTrigger, 0, nameCount(TheFXTriggerNames) - 1, "a damage record's FX trigger");
	x.xferUnsignedInt(&in.m_shockWaveSourceID);
	coord3D(x, in.m_shockWaveVector);
	x.xferReal(&in.m_shockWaveAmount);
	x.xferReal(&in.m_shockWaveRadius);
	x.xferReal(&in.m_shockWaveTaperOff);
	x.xferReal(&in.m_shockWaveZMult);
	x.xferBool(&in.m_shockWaveClearRadius);
	x.xferReal(&in.m_shockWaveClearMult);
	x.xferReal(&in.m_shockWaveClearFlingHeight);
	coord3D(x, in.m_shockWaveClearCenter);
	x.xferReal(&in.m_cyclonicFactor);
}

void damageInfo(Xfer &x, DamageInfo &info)
{
	damageInput(x, info.m_input);
	x.xferReal(&info.m_output.m_actualDamageDealt);
	x.xferReal(&info.m_output.m_actualDamageClipped);
	x.xferBool(&info.m_output.m_noEffect);
}

namespace
{
// ids are plain numbers here (a list of ids is not a link: the code that reads one looks each id up when it uses it); a set refuses a repeated id
template <typename C>
void idContainer(Xfer &x, C &c, bool unique)
{
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)c.size();
	x.xferCount(&n, 4, entryMemory<C>());
	if (!x.isLoading())
	{
		for (ObjectID id : c)
		{
			x.xferObjectID(&id);
		}
		return;
	}
	C loaded;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		ObjectID id = 0;
		x.xferObjectID(&id);
		const size_t before = loaded.size();
		loaded.insert(loaded.end(), id);
		if (unique && loaded.size() == before)
		{
			throw XferException("an id set lists object id " + std::to_string(id) + " twice");
		}
	}
	c.swap(loaded);
}
} // namespace

void stlObjectIDList(Xfer &x, std::list<ObjectID> &v)
{
	version(x, 1, 1);
	if (!x.isLoading() && v.size() > 0xFFFFu)
	{
		throw XferException("an object id list of " + std::to_string(v.size()) + " entries (the retail list holds at most 65535)");
	}
	std::uint16_t n = x.isLoading() ? 0 : (std::uint16_t)v.size();
	x.xferUnsignedShort(&n);
	if (!x.isLoading())
	{
		for (ObjectID id : v)
		{
			x.xferObjectID(&id);
		}
		return;
	}
	x.checkLoadedCount(n, 4, entryMemory<std::list<ObjectID>>()); // retail's u16 count, checked and charged like any count
	std::list<ObjectID> loaded;
	for (std::uint16_t i = 0; i < n; ++i)
	{
		ObjectID id = 0;
		x.xferObjectID(&id);
		loaded.push_back(id);
	}
	v.swap(loaded);
}

void ids(Xfer &x, std::vector<ObjectID> &v) { idContainer(x, v, false); }
void ids(Xfer &x, std::list<ObjectID> &v) { idContainer(x, v, false); }
void ids(Xfer &x, std::deque<ObjectID> &v) { idContainer(x, v, false); }
void ids(Xfer &x, std::set<ObjectID> &v) { idContainer(x, v, true); }

void ints(Xfer &x, std::vector<int> &v)
{
	each(x, v, 4, [&](int &e) { x.xferInt(&e); });
}

void uints(Xfer &x, std::vector<std::uint32_t> &v)
{
	each(x, v, 4, [&](std::uint32_t &e) { x.xferUnsignedInt(&e); });
}

void reals(Xfer &x, std::vector<float> &v)
{
	each(x, v, 4, [&](float &e) { x.xferReal(&e); });
}

void strings(Xfer &x, std::vector<std::string> &v)
{
	each(x, v, 1, [&](std::string &e) { x.xferAsciiString(&e); });
}

void strings(Xfer &x, std::set<std::string> &v)
{
	std::vector<std::string> flat;
	if (!x.isLoading())
	{
		flat.assign(v.begin(), v.end());
	}
	strings(x, flat);
	if (x.isLoading())
	{
		std::set<std::string> loaded;
		for (const std::string &e : flat)
		{
			if (!loaded.insert(e).second)
			{
				throw XferException("a name set lists '" + e + "' twice");
			}
		}
		v.swap(loaded);
	}
}

void bools(Xfer &x, std::vector<bool> &v)
{
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)v.size();
	x.xferCount(&n, 1, 1);
	std::vector<bool> loaded(x.isLoading() ? n : 0, false);
	std::vector<bool> &use = x.isLoading() ? loaded : v;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		bool b = x.isLoading() ? false : (bool)use[i];
		x.xferBool(&b);
		use[i] = b;
	}
	if (x.isLoading())
	{
		v.swap(loaded);
	}
}

void coords(Xfer &x, std::vector<Coord3D> &v)
{
	each(x, v, 12, [&](Coord3D &e) { coord3D(x, e); });
}

Object *findObject(GameLogic &logic, ObjectID id, const char *what)
{
	if (id == INVALID_ID)
	{
		return nullptr;
	}
	const std::set<ObjectID> *table = logic.snapshotObjectTable();
	Object *o = (table && !table->count(id)) ? nullptr : logic.findObjectByID(id);
	if (!o)
	{
		throw XferException(std::string(what) + ": object id " + std::to_string(id) + " is not an object of the snapshot");
	}
	return o;
}

void objectPointers(Xfer &x, GameLogic &logic, std::list<Object *> &list, const char *what, ObjectID owner)
{
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)list.size();
	x.xferCount(&n, 4, entryMemory<std::list<Object *>>());
	if (!x.isLoading())
	{
		for (Object *o : list)
		{
			ObjectID id = o ? o->getID() : (ObjectID)INVALID_ID;
			x.xferObjectID(&id);
		}
		return;
	}
	std::vector<ObjectID> ids;
	std::vector<Object *> objects;
	std::set<ObjectID> seen;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		ObjectID id = 0;
		x.xferObjectID(&id);
		if (id == INVALID_ID)
		{
			throw XferException(std::string(what) + ": an entry names no object");
		}
		if (!seen.insert(id).second)
		{
			throw XferException(std::string(what) + ": object id " + std::to_string(id) + " is listed twice");
		}
		objects.push_back(findObject(logic, id, what));
		ids.push_back(id);
	}
	if (logic.stagingSnapshotLinks())
	{
		GameLogic::SnapshotLink link;
		link.role = GameLogic::SnapshotLink::RIDERS;
		link.owner = owner;
		link.ids = ids;
		link.what = what;
		link.install = [&list](const std::vector<Object *> &objs) { list.assign(objs.begin(), objs.end()); };
		logic.stageSnapshotLink(std::move(link));
		return;
	}
	list.assign(objects.begin(), objects.end());
}

void containerPointer(Xfer &x, GameLogic &logic, Object *&container, ObjectID owner)
{
	ObjectID id = (!x.isLoading() && container) ? container->getID() : (ObjectID)INVALID_ID;
	x.xferObjectID(&id);
	if (!x.isLoading())
	{
		return;
	}
	Object *o = findObject(logic, id, "the object's container");
	if (o && o->getID() == owner)
	{
		throw XferException("the snapshot's object " + std::to_string(owner) + " is its own container");
	}
	if (logic.stagingSnapshotLinks())
	{
		GameLogic::SnapshotLink link;
		link.role = GameLogic::SnapshotLink::CONTAINED_BY;
		link.owner = owner;
		link.ids = { id };
		link.what = "the object's container";
		link.install = [&container](const std::vector<Object *> &objs) { container = objs.empty() ? nullptr : objs[0]; };
		logic.stageSnapshotLink(std::move(link));
		return;
	}
	container = o;
}

void thingTemplate(Xfer &x, GameLogic &logic, const ThingTemplate *&t)
{
	std::string name = (!x.isLoading() && t) ? t->getName() : std::string();
	x.xferAsciiString(&name);
	if (!x.isLoading())
	{
		return;
	}
	if (name.empty())
	{
		t = nullptr;
		return;
	}
	const ThingTemplate *found = logic.things().findTemplate(name);
	if (!found)
	{
		throw XferException("the object template '" + name + "' is not in this game's data");
	}
	t = found->getFinalOverride();
}

void weaponTemplate(Xfer &x, const WeaponTemplate *&t)
{
	std::string name = (!x.isLoading() && t) ? t->getName() : std::string();
	x.xferAsciiString(&name);
	if (!x.isLoading())
	{
		return;
	}
	t = nullptr;
	if (!name.empty())
	{
		t = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
		if (!t)
		{
			throw XferException("the weapon '" + name + "' is not in this game's data");
		}
	}
}

void upgradeTemplate(Xfer &x, const UpgradeTemplate *&t)
{
	std::string name = (!x.isLoading() && t) ? t->getUpgradeName() : std::string();
	x.xferAsciiString(&name);
	if (!x.isLoading())
	{
		return;
	}
	t = nullptr;
	if (!name.empty())
	{
		t = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
		if (!t)
		{
			throw XferException("the upgrade '" + name + "' is not in this game's data");
		}
	}
}

void specialPowerTemplate(Xfer &x, const SpecialPowerTemplate *&t)
{
	std::string name = (!x.isLoading() && t) ? t->getName() : std::string();
	x.xferAsciiString(&name);
	if (!x.isLoading())
	{
		return;
	}
	t = nullptr;
	if (!name.empty())
	{
		t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(name) : nullptr;
		if (!t)
		{
			throw XferException("the special power '" + name + "' is not in this game's data");
		}
	}
}

void checkRange(std::int64_t v, std::int64_t lo, std::int64_t hi, const std::string &what)
{
	if (SnapshotProbe::hit(what)) // lane SAVE-4e: a test's out-of-set value here
	{
		v = hi < INT64_MAX ? hi + 1 : lo - 1;
	}
	if (v < lo || v > hi)
	{
		throw XferException(what + " " + std::to_string(v) + " is not one of " + std::to_string(lo) + " .. " + std::to_string(hi));
	}
}

std::string hex32(std::uint64_t v)
{
	char b[24];
	std::snprintf(b, sizeof(b), "0x%08llX", (unsigned long long)v);
	return b;
}

int modelConditionCount() { return nameCount(TheModelConditionNames); }
int objectStatusCount() { return nameCount(TheObjectStatusNames); }
int weaponConditionCount() { return nameCount(TheWeaponConditionNames); }
int kindOfCount() { return nameCount(KindOfTokens::names()); } // lane MODS-3: the table in force

int nameCount(const char *const *names)
{
	int n = 0;
	while (names[n])
	{
		++n;
	}
	return n;
}

void version(Xfer &x, std::uint8_t earliest, std::uint8_t current)
{
	XferVersion v{ earliest, current };
	x.xferVersion(v);
}
} // namespace SnapshotXfer
