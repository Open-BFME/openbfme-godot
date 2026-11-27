// OpenBFME. GPL-3.0.
// See GameLogic/Module/DestroyEnvironmentUpdate.h (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06).

// Local 169: RW 0x8AFC93 (StartTime)
#include "GameLogic/Module/DestroyEnvironmentUpdate.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define DEU_OFF(m) (int)offsetof(DestroyEnvironmentUpdateModuleData, m)
// RW 0xC6B844 / BFME2 0xC54D28
const FieldParse kDestroyEnvironmentUpdateFieldParse[] = {
	{ "StartTime", INI::parseDurationUnsignedInt, nullptr, DEU_OFF(m_startTime) },
	{ "DestructionTime", INI::parseDurationUnsignedInt, nullptr, DEU_OFF(m_destructionTime) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DEU_OFF
} // namespace

// RW 0x8AFC7F (ctor)
void DestroyEnvironmentUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDestroyEnvironmentUpdateFieldParse);
}

DestroyEnvironmentUpdate::DestroyEnvironmentUpdate(Thing *thing, const DestroyEnvironmentUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (getObject())
	{
		m_startFrame = getObject()->logic().getFrame();
	}
}

DestroyEnvironmentUpdate::~DestroyEnvironmentUpdate()
{
	releaseEnvironmentObject();
}

// RW 0x8AFC5F / BFME2 0x4AC5F5
EnvironmentActor *DestroyEnvironmentUpdate::findEnvironmentActor(Object *obj)
{
	if (!obj)
	{
		return nullptr;
	}
	for (auto &m : obj->modules())
	{
		if (auto *bm = dynamic_cast<BehaviorModule *>(m.get()))
		{
			if (EnvironmentActor *actor = bm->slot31())
			{
				return actor;
			}
		}
	}
	return nullptr;
}

// RW 0x8AFD8C / BFME2 0x4AC722
void DestroyEnvironmentUpdate::releaseEnvironmentObject()
{
	if (m_objectID == 0)
	{
		return;
	}
	if (getObject())
	{
		Object *obj = getObject()->logic().findObjectByID(m_objectID);
		if (obj)
		{
			if (EnvironmentActor *actor = findEnvironmentActor(obj))
			{
				actor->release(0.0f);
			}
			getObject()->logic().destroyObject(obj);
		}
	}
	m_objectID = 0;
}

// RW 0x8AFE33 / BFME2 0x4AC7C9
ObjectID DestroyEnvironmentUpdate::findEnvironmentObject()
{
	// Queries partition for KINDOF_ENVIRONMENT (0x65) whose behavior modules return non-null from slot 31 (RW 0x8AFC5F).
	if (!getObject())
	{
		return 0;
	}
	GameLogic &logic = getObject()->logic();
	static const int envKind = ObjectTemplateInfoBuilder::kindOfIndex("ENVIRONMENT");
	if (envKind < 0)
	{
		return 0;
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOf((unsigned)envKind) && !o->isDestroyed())
		{
			if (findEnvironmentActor(o) != nullptr)
			{
				return o->getID();
			}
		}
	}
	return 0;
}

// RW 0x8AFEB5 / BFME2 0x4AC84B
UpdateSleepTime DestroyEnvironmentUpdate::update()
{
	if (!m_data)
	{
		return UPDATE_SLEEP_FOREVER;
	}

	if (!m_objectID)
	{
		m_objectID = findEnvironmentObject();
		if (!m_objectID)
		{
			return UPDATE_SLEEP_FOREVER;
		}
	}

	unsigned int elapsed = 0;
	if (getObject())
	{
		unsigned int frame = getObject()->logic().getFrame();
		if (frame >= m_startFrame)
		{
			elapsed = frame - m_startFrame;
		}
	}

	if (elapsed < m_data->m_startTime)
	{
		return (UpdateSleepTime)(m_data->m_startTime - elapsed);
	}

	elapsed -= m_data->m_startTime;
	if (elapsed >= m_data->m_destructionTime)
	{
		releaseEnvironmentObject();
	}
	else
	{
		// RW 0x8AFEB5 / BFME2 0x4AC84B: actor->advance(-1.0f / (float)m_data->m_destructionTime)
		if (getObject())
		{
			Object *obj = getObject()->logic().findObjectByID(m_objectID);
			if (EnvironmentActor *actor = findEnvironmentActor(obj))
			{
				actor->advance(SimMath::divf32(-1.0f, SimMath::sseFromInt32((int)m_data->m_destructionTime)));
			}
		}
	}

	return UPDATE_SLEEP_NONE;
}

void DestroyEnvironmentUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_startFrame);
	x.xferObjectID(&m_objectID);
}

void DestroyEnvironmentUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<DestroyEnvironmentUpdateModuleData>("DestroyEnvironmentUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("DestroyEnvironmentUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const DestroyEnvironmentUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("DestroyEnvironmentUpdate: module data is not typed");
			}
			return std::make_unique<DestroyEnvironmentUpdate>(thing, typed);
		});
}
