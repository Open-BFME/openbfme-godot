// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraCreateModules.h for the target facts, the addresses and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/Object/PartitionManager.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>

namespace
{
const FieldParse kExperienceLevelCreate[] = {
	{ "LevelToGrant", INI::parseInt, nullptr, (int)offsetof(ExperienceLevelCreateModuleData, m_levelToGrant) },
	{ "MPOnly", INI::parseBool, nullptr, (int)offsetof(ExperienceLevelCreateModuleData, m_mpOnly) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC16928
const LookupListRec kWeaponSlots[] = { { "PRIMARY", 0 }, { "SECONDARY", 1 }, { "TERTIARY", 2 }, { "QUATERNARY", 3 }, { "QUINARY", 4 }, { nullptr, 0 } };
const FieldParse kLockWeaponCreate[] = {
	{ "SlotToLock", INI::parseLookupList, kWeaponSlots, (int)offsetof(LockWeaponCreateModuleData, m_slotToLock) },
	{ nullptr, nullptr, nullptr, 0 }
};

void parseUpgradeMaskField(INI *ini, void *, void *store, const void *)
{
	UpgradeCenter::parseUpgradeMask(ini, *static_cast<UpgradeMaskType *>(store), nullptr); // RW 0x66F603
}
const FieldParse kInheritUpgradeCreate[] = {
	{ "Radius", INI::parseReal, nullptr, (int)offsetof(InheritUpgradeCreateModuleData, m_radius) },
	{ "Upgrade", parseUpgradeMaskField, nullptr, (int)offsetof(InheritUpgradeCreateModuleData, m_upgrade) },
	{ "ObjectFilter", ParseObjectFilter, nullptr, (int)offsetof(InheritUpgradeCreateModuleData, m_objectFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void ExperienceLevelCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kExperienceLevelCreate); // RW 0xC7026C
}
void LockWeaponCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLockWeaponCreate); // RW 0xC6FFF0
}
void InheritUpgradeCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kInheritUpgradeCreate); // RW 0xC703E0
}

// ---- ExperienceLevelCreate ----------------------------------------------------------------------------------------------------------
// RW 0x8BD375 / 0x8BD342
void ExperienceLevelCreate::onBuildComplete()
{
	Object *obj = getObject();
	if (m_data->m_mpOnly && !obj->logic().economy().isMultiplayerGame()) // RW 0x625456
	{
		return;
	}
	if (ExperienceTracker *xp = obj->getExperienceTracker())
	{
		xp->gainExpForLevel(m_data->m_levelToGrant - xp->getRank(), false, false); // RW 0x79DA0A
	}
}

// ---- LockWeaponCreate ---------------------------------------------------------------------------------------------------------------
// RW 0x8BCE0A -> Object::setWeaponLock RW 0x69121A
void LockWeaponCreate::onBuildComplete()
{
	m_needToRunOnBuildComplete = false;
	Object *obj = getObject();
	if (obj->getContain())
	{
		obj->logic().noteStop("[S-981] LockWeaponCreate: the contain module's weapon lock pass-through (RW 0x69121A, contain slot 0x168) is not ported");
	}
	static const int kSwitchedWeapons = ObjectTemplateInfoBuilder::objectStatusIndex("SWITCHED_WEAPONS"); // RW status 0x51
	if (kSwitchedWeapons >= 0)
	{
		obj->setStatus((unsigned)kSwitchedWeapons, m_data->m_slotToLock != 0); // permanent lock: set when the slot is not PRIMARY
	}
	if (ObjectWeapons *w = obj->getWeapons())
	{
		w->setWeaponLock(m_data->m_slotToLock, LOCKED_PERMANENTLY); // RW 0x6C97F9
	}
}

void LockWeaponCreate::crc(StateHasher &hasher) const
{
	hasher.addBool(m_needToRunOnBuildComplete);
}

// ---- InheritUpgradeCreate -----------------------------------------------------------------------------------------------------------
// RW 0x8BD80E / 0x8BD701
void InheritUpgradeCreate::onBuildComplete()
{
	if (!m_needToRunOnBuildComplete)
	{
		return;
	}
	m_needToRunOnBuildComplete = false;
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Player *own = obj->getControllingPlayer();
	// RW 0x8BD76C: ThePartitionManager within Radius, distance type 1 (FROM_CENTER_3D), unsorted, with the ObjectFilter as the one partition filter (vtable RW 0xBE4CC8,
	// the owner's view); every hit then needs the same controlling player and all the upgrade bits (lane MODULES-2)
	PartitionFilterFn filter([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_objectFilter, o, own); });
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_radius, FROM_CENTER_3D, { &filter }, ITER_FASTEST))
	{
		Object *other = hit.object;
		if (other->getControllingPlayer() != own || !other->getUpgradeMask().testForAll(m_data->m_upgrade)) // RW 0x8BD792 / 0x6AACB3
		{
			continue;
		}
		for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit) // RW 0x8BD7AE .. 0x8BD7DE (0x480 bits)
		{
			if (!m_data->m_upgrade.test(bit))
			{
				continue;
			}
			const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgradeByMaskBit((int)bit) : nullptr; // RW 0x66F218
			obj->giveUpgrade(u); // RW 0x69388B
			++m_inherited;
		}
	}
}

void InheritUpgradeCreate::crc(StateHasher &hasher) const
{
	hasher.addBool(m_needToRunOnBuildComplete);
	hasher.addU32(m_inherited);
}
