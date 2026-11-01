// OpenBFME. GPL-3.0.
// See GameLogic/Module/GrabPassengerSpecialPower.h for the target facts and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GrabPassengerSpecialPower.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace
{
typedef GrabPassengerSpecialPowerModuleData Data;

const unsigned kOptionUngated = 0x40000u; // RW 0x8C94DE `test` of the options' bit 18 (as SpecialPowerModule's RW 0x8980A6)
const int kGrabPassenger = 0x27;          // SPECIAL_GRAB_PASSENGER

// RW 0xC74534
const FieldParse kFields[] = {
	{ "GrabRadius", INI::parseReal, nullptr, (int)offsetof(Data, m_grabRadius) }, // RW 0x42ED00
	{ "AllowTree", INI::parseBool, nullptr, (int)offsetof(Data, m_allowTree) },   // RW 0x42E558
	{ nullptr, nullptr, nullptr, 0 }
};

int kind(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

bool hasKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}
} // namespace

void GrabPassengerSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p); // RW 0xC64DB0
	p.add(kFields);
}

void GrabPassengerSpecialPower::registerClass(ModuleFactory &modules)
{
	static const char kName[] = "GrabPassengerSpecialPower";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<GrabPassengerSpecialPower>(thing, typed);
	});
}

// RW 0x8C942D
GrabPassengerSpecialPower::GrabPassengerSpecialPower(Thing *thing, const Data *data)
	: SpecialPowerModule(thing, data)
	, m_data(data)
{
}

SpecialPowerModuleInterface *GrabPassengerSpecialPower::moduleOfType(int type) const
{
	for (const std::unique_ptr<BehaviorModule> &m : getObject()->modules()) // RW 0x691526
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (t && t->getSpecialPowerType() == type)
		{
			return sp;
		}
	}
	return nullptr;
}

// RW 0x8C94D7
void GrabPassengerSpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	Object *obj = getObject();
	if (!target)
	{
		return; // RW reads the target's template unchecked; every caller passes one
	}
	if ((options & kOptionUngated) == 0 && (pauseCount() > 0 || obj->getDisabledMask() != DISABLEDMASK_NONE)) // + 0x1C; RW 0x9325B4
	{
		return;
	}
	// the grab check (power type 0x27 only): the object's 0x27 module and RW 0x82D925 against `against`
	auto grabAllowed = [&](Object *against) -> bool {
		const SpecialPowerTemplate *mine = getSpecialPowerTemplate(); // slot 0x18
		if (!mine || mine->getFinalOverride()->getSpecialPowerType() != kGrabPassenger) // RW 0x688D3C -> + 0x1C
		{
			return true;
		}
		SpecialPowerModuleInterface *grab = moduleOfType(kGrabPassenger); // RW 0x691526(0x27)
		if (!grab)
		{
			return false;
		}
		const CommandSourceType source = (options >> 18) & 1u ? CMD_FROM_SCRIPT : CMD_FROM_PLAYER; // (options >> 0x12) & 1
		return ActionManager::canDoSpecialPowerAtObject(*obj, against, source, grab->getSpecialPowerTemplate(), options, true);
	};
	static const int kHorde = kind("HORDE"); // template + 0x115 bit 5
	if (!hasKind(*target, kHorde))
	{
		if (!grabAllowed(target))
		{
			return;
		}
	}
	else
	{
		ContainModuleInterface *contain = target->getContain(); // Object + 0x258 (RW calls its slot 0x78 unchecked)
		if (!contain)
		{
			throw std::logic_error("GrabPassengerSpecialPower: the HORDE target " + target->getTemplate()->getName() + " has no contain (RW 0x8C9561 calls it)");
		}
		if (HordeContain *horde = dynamic_cast<HordeContain *>(contain)) // slot 0x78
		{
			Object *member = horde->pickMemberNoPosition(true); // slot 0x48(1, 0, 0, 0, 0): the first member not TAGGED
			if (!member)
			{
				return;
			}
			if (!grabAllowed(target))
			{
				return;
			}
			static const int kTagged = ObjectTemplateInfoBuilder::objectStatusIndex("TAGGED"); // status 0x3F
			if (kTagged < 0)
			{
				throw std::logic_error("GrabPassengerSpecialPower: no TAGGED object status");
			}
			member->setStatus((unsigned)kTagged, true); // RW 0x62684D(0x3F, 1)
			target = member;
		}
	}
	(void)initiate(target, nullptr, options); // RW 0x897E87(target, 0, options, 0): RW ignores its answer (the gate above is the same one)
	++m_started;
	if (!spData()->m_updateModuleStartsAttack) // data + 0xC
	{
		const Coord3D pos = *target->getPosition();
		triggerSpecialPower(&pos, target); // RW 0x897987(target + 0x38)
	}
}

// RW 0x8C9629
void GrabPassengerSpecialPower::doSpecialPower(unsigned options)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Coord3D pos = *obj->getPosition();
	Object *target = nullptr;
	Coord3D point{};
	bool usePoint = false;
	if (options & 0x8000u)
	{
		// a rock or a tree: the closest grabbable tree (AllowTree) and the closest ROCK / ROCK_VENDOR object
		const TerrainTrees::Record *tree = m_data->m_allowTree ? logic.trees().findClosestTree(pos, m_data->m_grabRadius, true, 1) : nullptr; // RW 0x67F52B(.., 1, 1)
		static const int kRock = kind("ROCK"), kRockVendor = kind("ROCK_VENDOR"); // KindOf 0x61 / 0x88
		PartitionFilterFn rocks([&](Object &o) { return hasKind(o, kRock) || hasKind(o, kRockVendor); });
		target = logic.partition().getClosestObject(pos, m_data->m_grabRadius, FROM_CENTER_2D, { &rocks });
		if (tree)
		{
			usePoint = true;
			point = tree->position;
			if (target)
			{
				const Coord3D &t = *target->getPosition();
				const float pointDist = (float)SimMath::length3d(SimMath::subf32(pos.x, point.x), SimMath::subf32(pos.y, point.y), SimMath::subf32(pos.z, point.z));
				const float targetDist = (float)SimMath::length3d(SimMath::subf32(pos.x, t.x), SimMath::subf32(pos.y, t.y), SimMath::subf32(pos.z, t.z));
				usePoint = !(targetDist < pointDist); // RW 0x8C9852: the object only when strictly nearer
			}
		}
	}
	else if (options & 0x2000u)
	{
		static const int kRockVendor = kind("ROCK_VENDOR"); // KindOf 0x88
		PartitionFilterFn vendors([&](Object &o) { return hasKind(o, kRockVendor); });
		target = logic.partition().getClosestObject(pos, m_data->m_grabRadius, FROM_CENTER_2D, { &vendors });
	}
	else
	{
		// INFANTRY (KindOf 8), not UNSELECTABLE (RW 0xC1D6A0 with status 3), the object's ENEMIES (RW 0xC11DC0(obj, 1, false))
		static const int kInfantry = kind("INFANTRY");
		static const int kUnselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
		PartitionFilterFn prey([&](Object &o) {
			return hasKind(o, kInfantry) && !(kUnselectable >= 0 && o.testStatus((unsigned)kUnselectable)) && obj->getRelationship(o) == ENEMIES;
		});
		target = logic.partition().getClosestObject(pos, m_data->m_grabRadius, FROM_CENTER_2D, { &prey });
	}
	SpecialPowerModuleInterface *grab = moduleOfType(kGrabPassenger); // RW 0x691526(0x27)
	if (!grab)
	{
		return;
	}
	if (usePoint)
	{
		++m_autoAtTree;
		grab->doSpecialPowerAtLocation(point, 0x2000u); // slot 0x30
	}
	else if (target)
	{
		++m_autoAtObject;
		grab->doSpecialPowerAtObject(target, 0x2000u); // slot 0x2C
	}
}

void GrabPassengerSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU64(m_started);
	h.addU64(m_autoAtObject);
	h.addU64(m_autoAtTree);
}

// OpenBFME native snapshot (S-4190): SpecialPowerModule's, then the counters (RW's module has no field of its own)
void GrabPassengerSpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SpecialPowerModule::xfer(x);
	SnapshotXfer::u64(x, m_started);
	SnapshotXfer::u64(x, m_autoAtObject);
	SnapshotXfer::u64(x, m_autoAtTree);
}
