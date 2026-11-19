// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraCreateModules.h for the target facts, the addresses and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/SnapshotXfer.h"
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
	// RW 0x8BCE0A: clear the flag byte, then push 2 (LOCKED_PERMANENTLY), push [data+8] (SlotToLock), call Object::setWeaponLock RW 0x69121A —
	// the contain pass-through (contain slot 0x168), SWITCHED_WEAPONS (status 0x51) = (type == 2 and slot != 0) and the weapon set's lock RW 0x6C97F9
	// all live in ObjectWeapons::setObjectWeaponLock; one path for one action.
	ObjectWeapons::setObjectWeaponLock(*obj, m_data->m_slotToLock, LOCKED_PERMANENTLY);
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

// ---- lane SAVE-4b: the snapshots (OpenBFME native, S-4190) ------------------------------------------------------------------------------------------------
void ExperienceLevelCreate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

void LockWeaponCreate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferBool(&m_needToRunOnBuildComplete);
}

// RW 0x8BD80E / 0x8BD701: onBuildComplete checks shouldDoOnBuildComplete (slot 3 at +0xC, RW 0x4986C4) then clears +0x14 flag and runs partition query.
// DONOR BFME2 0x004B94E3 (InheritUpgradeCreateXfer.cpp): base CreateModule xfer via 0x4B8D4E then Version(1,1) (base first).
// Missing functions cited: RW 0x8BD5B1 (name literal 0xC0ABA0), RW 0x8BD5D6 (ModuleData table 0xC703E0), RW 0x8BD62C (scalar deleting dtor), RW 0x8BD648 (filter helper), RW 0x8BD829 (thunk add ecx -0x10 -> 0x8BD701).
// xfer: BehaviorModule xfer (RW 0x4C9C7D via CreateModule 0x4B8D4E) then Version(1,1); m_inherited is extra test state (not in retail) kept after version.
void InheritUpgradeCreate::xfer(Xfer &x)
{
	xferBehaviorModule(x);
	SnapshotXfer::version(x, 1, 1);
	x.xferBool(&m_needToRunOnBuildComplete);
	x.xferUnsignedInt(&m_inherited);
}
