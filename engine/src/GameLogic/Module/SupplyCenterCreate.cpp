// OpenBFME. GPL-3.0.
// See GameLogic/Module/SupplyCenterCreate.h.

#include "GameLogic/Module/SupplyCenterCreate.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ResourceGatheringManager.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <stdexcept>

namespace
{
const FieldParse kSupplyCenterCreateFieldParse[] = { { nullptr, nullptr, nullptr, 0 } }; // the class's table holds no field
} // namespace

void SupplyCenterCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSupplyCenterCreateFieldParse);
}

void SupplyCenterCreate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<SupplyCenterCreateModuleData>("SupplyCenterCreate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SupplyCenterCreate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SupplyCenterCreateModuleData *typed = dynamic_cast<const SupplyCenterCreateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("SupplyCenterCreate: the module data is not typed");
		}
		return std::make_unique<SupplyCenterCreate>(thing, typed);
	});
}

// RW 0x8BCF6C
void SupplyCenterCreate::onBuildComplete()
{
	if (!m_needToRunOnBuildComplete) // RW 0x4986C4
	{
		return;
	}
	m_needToRunOnBuildComplete = false;
	Object *obj = getObject();
	PlayerList &players = obj->logic().players();
	for (int i = players.getPlayerCount() - 1; i >= 0; --i)
	{
		Player *p = players.getNthPlayer(i); // RW 0x6A844E
		if (p)
		{
			p->resourceGatheringManager().addSupplyCenter(obj); // Player + 0x304, RW 0x8FAA11
		}
	}
}

void SupplyCenterCreate::crc(StateHasher &h) const
{
	h.addBool(m_needToRunOnBuildComplete);
}

// RW 0x8BD56C: version 1, then the create module base (RW 0x8BCE8F: the flag)
void SupplyCenterCreate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferBool(&m_needToRunOnBuildComplete);
}
