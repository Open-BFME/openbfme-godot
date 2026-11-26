// OpenBFME. GPL-3.0. See SpawnUnitBehavior.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpawnUnitBehavior.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
typedef SpawnUnitBehaviorModuleData Data;
}

void SpawnUnitBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x8BC981 -> 0xC6FEF0; defaults RW 0x65573A.
	static const FieldParse fields[] = {
		{ "UnitName", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitName) },
		{ "UnitCommand", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitCommand) },
		{ "SpawnOnce", INI::parseBool, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_spawnOnce) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(fields);
}

void SpawnUnitBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "SpawnUnitBehavior";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<SpawnUnitBehavior>(thing, typed);
	});
}

SpawnUnitBehavior::SpawnUnitBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_oncePressPending(data->m_spawnOnce) // RW 0x8BC959
{
}

UpdateSleepTime SpawnUnitBehavior::update()
{
	const UpdateSleepTime again = UPDATE_SLEEP((int)LOGICFRAMES_PER_SECOND); // [RW 0xD9F608] on every return
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *player = obj->getControllingPlayer(); // RW 0x68B678
	if (!player || logic.getFrame() < (UnsignedInt)LOGICFRAMES_PER_SECOND)
	{
		return again;
	}
	// Player + 0x755 (TOGGLE_AUTO_BUILD's flag) is always set here: the port has no such script action
	static const int kAwaiting = CombatNames::modelCondition("AWAITING_CONSTRUCTION");          // 67, Object + 0x114 bit 3
	static const int kBeingBuilt = CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED");   // 69, Object + 0x114 bit 5
	if (obj->testModelCondition(kAwaiting) || obj->testModelCondition(kBeingBuilt))
	{
		return again;
	}
	if (ProductionUpdateInterface *queue = obj->getProductionUpdate()) // RW 0x68C327(0)
	{
		if (queue->getProductionCount() > 0) // slot 17
		{
			if (queue->getProductionCount() <= 1)
			{
				return again;
			}
			const ProductionEntry *entry = queue->firstProduction(); // slot 21
			if (entry && entry->objectToProduce && entry->objectToProduce->getName() == m_data->m_unitName) // RW 0x4065AA
			{
				queue->cancelUnitCreate(entry->productionID); // slot 10
			}
			return again;
		}
	}
	const ThingTemplate *unit = logic.things().findTemplate(m_data->m_unitName); // RW 0x6D1305
	if (!unit || !player->canAffordCommandPoints(*unit))                         // RW 0x6A7F79 (Player + 0x60)
	{
		return again;
	}
	const CommandButton *button = TheCommandStore && !m_data->m_unitCommand.empty() ? TheCommandStore->findCommandButton(m_data->m_unitCommand) : nullptr; // RW 0x71D6EA
	if (m_data->m_spawnOnce && !m_oncePressPending)
	{
		return again;
	}
	if (button)
	{
		obj->doCommandButton(button, CMD_FROM_AI, false); // RW 0x696FD2(button, 2, 0)
	}
	m_oncePressPending = false; // RW 0x8BCAF7
	return again;
}

void SpawnUnitBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addBool(m_oncePressPending);
}

// RW 0x8BC8E4 (OpenBFME native snapshot, S-4190). Version 2 is the UpdateModule block and + 0x24, as retail's version 2. Version 1 is the block an OpenBFME build
// wrote while the update was the unported shell's (SpawnUnitBehavior's own version byte, then UnportedUpdateModule::xfer: its version byte, the UpdateModule
// block, asleep for good, and the shell's call count, a report counter that is dropped): the shell never pressed, so + 0x24 keeps the constructor's value
void SpawnUnitBehavior::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	if (v.current < 2)
	{
		SnapshotXfer::version(x, 1, 1); // UnportedUpdateModule's own
		xferUpdateModule(x);           // RW 0x850C60
		std::uint32_t shellCalls = 0;
		x.xferUnsignedInt(&shellCalls);
		return;
	}
	xferUpdateModule(x); // RW 0x850C60
	x.xferBool(&m_oncePressPending);
}
