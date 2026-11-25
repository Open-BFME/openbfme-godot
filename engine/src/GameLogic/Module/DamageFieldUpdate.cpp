// OpenBFME. GPL-3.0.

#include "GameLogic/Module/DamageFieldUpdate.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Weapon.h"

#include <algorithm>
#include <stdexcept>

namespace {

// RW 0xC64018 (DONOR BFME2 0xB44A00)
const FieldParse kDamageFieldUpdateFieldParse[] = {
	{ "Radius", INI::parseReal, nullptr, (int)offsetof(DamageFieldUpdateModuleData, m_radius) },
	{ "ObjectFilter", ParseObjectFilter, nullptr, (int)offsetof(DamageFieldUpdateModuleData, m_objectFilter) },
	{ "RequiredUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(DamageFieldUpdateModuleData, m_requiredUpgrade) },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

// RW 0x894773 / DONOR BFME2 0x491040
void DamageFieldUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	FireWeaponUpdateModuleData::buildFieldParse(p);
	p.add(kDamageFieldUpdateFieldParse);
}

// RW 0x894935 / DONOR BFME2 0x491220 (retail m_victims std::list<ObjectID> helpers: ctor RW 0x006A057C, _M_create_node RW 0x006A05C1, insert RW 0x006A1D75, erase RW 0x006AC552, remove RW 0x006AC9F9)
DamageFieldUpdate::DamageFieldUpdate(Thing *thing, const DamageFieldUpdateModuleData *data)
	: FireWeaponUpdate(thing, data)
	, m_data(data)
{
}

// RW 0x008948B8 (~DamageFieldUpdate -> FireWeaponUpdate::~FireWeaponUpdate RW 0x0088F37D), RW 0x00894986 (scalar deleting destructor)
DamageFieldUpdate::~DamageFieldUpdate() = default;

// RW 0x89483A / DONOR BFME2 0x4911AE
void DamageFieldUpdate::fireExtraWeapon(Object *other)
{
	if (!other)
	{
		return;
	}
	if (m_weapon)
	{
		if (weaponHost().extraWeaponStatus(*m_weapon) == WEAPON_READY_TO_FIRE)
		{
			weaponHost().forceFireExtraWeapon(*m_weapon, *other); // RW 0x00894860 -> RW 0x006CF407
			++m_shots;
		}
		return;
	}
	const std::uint32_t now = getObject()->logic().getFrame();
	for (Record &r : m_records)
	{
		if (!(r.fireFrame < now) || weaponHost().extraWeaponStatus(*r.weapon) != WEAPON_READY_TO_FIRE)
		{
			continue;
		}
		weaponHost().forceFireExtraWeapon(*r.weapon, *other); // RW 0x0089489B -> RW 0x006CF407
		++m_shots;
		if (r.oneShot)
		{
			r.fireFrame = 0xFFFFFFFFu;
		}
	}
}

// RW 0x8949A2 / DONOR BFME2 0x491316 (DamageFieldUpdateRva004911AE.cpp:195)
UpdateSleepTime DamageFieldUpdate::update()
{
	Object *me = getObject();
	if (!m_data || !me)
	{
		return nextSleep();
	}

	if (!m_data->m_requiredUpgrade.empty())
	{
		const UpgradeTemplate *upgrade = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_data->m_requiredUpgrade) : nullptr;
		if (upgrade && !me->hasUpgrade(upgrade))
		{
			return UPDATE_SLEEP_NONE; // RW 0x8949FD
		}
	}

	GameLogic &logic = me->logic();
	Player *controllingPlayer = me->getControllingPlayer();
	const PartitionHits hits = logic.partition().iterateObjectsInRange(
		*me->getPosition(), m_data->m_radius, FROM_CENTER_2D, {}, ITER_FASTEST);

	std::vector<ObjectID> inRange;
	inRange.reserve(hits.size());

	for (const PartitionHit &h : hits)
	{
		Object *other = h.object;
		if (!other || other == me || other->isEffectivelyDead() || !other->isInWorld())
		{
			continue;
		}
		if (!ObjectFilterMatch::allows(logic, m_data->m_objectFilter, *other, controllingPlayer))
		{
			continue;
		}
		ObjectID id = other->getID();
		inRange.push_back(id);
		bool alreadyVictim = std::find(m_victims.begin(), m_victims.end(), id) != m_victims.end();
		if (!alreadyVictim)
		{
			fireExtraWeapon(other);
			m_victims.push_back(id);
		}
	}

	m_victims.erase(
		std::remove_if(m_victims.begin(), m_victims.end(), [&](ObjectID id) {
			return std::find(inRange.begin(), inRange.end(), id) == inRange.end();
		}),
		m_victims.end());

	return nextSleep(); // RW 0x894BF5
}

// RW 0x00894C09 / DONOR BFME2 0x49157D
void DamageFieldUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	SnapshotXfer::ids(x, m_victims);
	FireWeaponUpdate::xfer(x);
}

// RW 0x894935 primary vtable slot 1 -> crc
void DamageFieldUpdate::crc(StateHasher &hasher) const
{
	for (ObjectID id : m_victims)
	{
		hasher.addU32(id);
	}
	FireWeaponUpdate::crc(hasher);
}

// RW 0x659082 / DONOR BFME2 0x6577D5; getModuleName RW 0x008948EA, getModuleNameKey RW 0x008948F0
void DamageFieldUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<DamageFieldUpdateModuleData>("DamageFieldUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(
		"DamageFieldUpdate",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const DamageFieldUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("DamageFieldUpdate: the module data is not typed");
			}
			return std::make_unique<DamageFieldUpdate>(thing, typed);
		});
}
