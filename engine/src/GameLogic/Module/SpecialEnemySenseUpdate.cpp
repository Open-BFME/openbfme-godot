// OpenBFME. GPL-3.0. Both editions: RW 0x89D7E6, BFME2 0x89A037.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace {
using Data = SpecialEnemySenseUpdateModuleData;
// RW 0xC07E98: filter, radius, then interval in milliseconds.
const FieldParse kFields[] = {
	// Local 164: RW 0x653E47 (SpecialEnemySenseUpdateModuleData::buildFieldParse)
	{ "SpecialEnemyFilter", ParseObjectFilter, nullptr, (int)offsetof(Data, m_filter) },
	{ "ScanRange", INI::parseReal, nullptr, (int)offsetof(Data, m_scanRange) },
	{ "ScanInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanInterval) },
	{ nullptr, nullptr, nullptr, 0 }
};
}

void SpecialEnemySenseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kFields); }

SpecialEnemySenseUpdate::SpecialEnemySenseUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data), m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x89D793
}

UpdateSleepTime SpecialEnemySenseUpdate::update()
{
	Object *owner = getObject();
	GameLogic &logic = owner->logic();
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
	PartitionFilterFn filter([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_filter, o, owner->getControllingPlayer()); }); // RW 0xBE4CC8
	PartitionFilterFn enemy([&](Object &o) { return owner->getRelationship(o) == ENEMIES; }); // RW 0xC11DC0, mask 1
	const bool found = logic.partition().getClosestObject(*owner->getPosition(), m_data->m_scanRange, FROM_CENTER_2D, { &alive, &filter, &enemy }) != nullptr;
	static const int condition = CombatNames::modelCondition("SPECIAL_ENEMY_NEAR");
	ContainModuleInterface *contain = owner->getContain();
	if (contain && contain->getHordeContainInterface())
	{
		// Horde interface slot 0x108: RW 0x872AB5 -> 0x865598, the contained list.
		const auto *items = contain->getContainedItemsList();
		if (items) for (Object *member : *items) member->setModelConditionState(condition, found);
	}
	else owner->setModelConditionState(condition, found);
	return UPDATE_SLEEP((int)m_data->m_scanInterval);
}

// RW 0x0089D7C5
void SpecialEnemySenseUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
}

void SpecialEnemySenseUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("SpecialEnemySenseUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SpecialEnemySenseUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const Data *>(data);
		if (!typed) throw std::logic_error("SpecialEnemySenseUpdate: the module data is not typed");
		return std::make_unique<SpecialEnemySenseUpdate>(thing, typed);
	});
}
