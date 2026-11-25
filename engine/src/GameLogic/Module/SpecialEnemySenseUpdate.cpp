// OpenBFME. GPL-3.0.
// See GameLogic/Module/SpecialEnemySenseUpdate.h for the target facts and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpecialEnemySenseUpdate.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
typedef SpecialEnemySenseUpdateModuleData Data;

const int kMcSpecialEnemyNear = 0x102; // TheModelConditionNames (RW 0xD9FAD8): SPECIAL_ENEMY_NEAR

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0xC07E98
const FieldParse kFields[] = {
	{ "SpecialEnemyFilter", parseFilter, nullptr, (int)offsetof(Data, m_specialEnemyFilter) },
	{ "ScanRange", INI::parseReal, nullptr, (int)offsetof(Data, m_scanRange) },
	{ "ScanInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanInterval) },
	{ nullptr, nullptr, nullptr, 0 }
};

void show(Object &obj, bool seen)
{
	if (obj.testModelCondition(kMcSpecialEnemyNear) != seen)
	{
		obj.setModelConditionState(kMcSpecialEnemyNear, seen); // RW 0x68B53C
	}
}
} // namespace

void SpecialEnemySenseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void SpecialEnemySenseUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "SpecialEnemySenseUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<SpecialEnemySenseUpdate>(thing, typed);
	});
}

// RW 0x89D758
SpecialEnemySenseUpdate::SpecialEnemySenseUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x89D793
}

// RW 0x89D7E6
UpdateSleepTime SpecialEnemySenseUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();                         // RW 0x68B678
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
	PartitionFilterFn special([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_specialEnemyFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
	PartitionFilterFn enemies([&](Object &o) { return obj->getRelationship(o) == ENEMIES; }); // RW 0xC11DC0(obj, 1, 0) -> RW 0x660B85 / 0x68D7AB
	const bool seen = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanRange, FROM_CENTER_2D, { &alive, &special, &enemies }) != nullptr; // RW 0xA39090
	ContainModuleInterface *contain = obj->getContain(); // Object + 0x258
	if (contain && contain->getHordeContainInterface())  // slot 0x7C
	{
		if (const ContainModuleInterface::ContainedItemsList *members = contain->getContainedItemsList()) // the horde interface's slot 0x108
		{
			for (Object *member : *members)
			{
				show(*member, seen); // RW 0x89D8AF .. 0x89D8C2 / 0x89D90E .. 0x89D921
			}
		}
	}
	else
	{
		show(*obj, seen); // RW 0x89D8D2 / 0x89D931
	}
	return UPDATE_SLEEP((int)m_data->m_scanInterval);
}

// RW 0x89D7C5 (OpenBFME native snapshot, S-4190)
void SpecialEnemySenseUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
}
