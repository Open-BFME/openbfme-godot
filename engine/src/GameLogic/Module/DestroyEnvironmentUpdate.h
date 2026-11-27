// OpenBFME. GPL-3.0.
//
// DestroyEnvironmentUpdate (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06):
// Slowly advances and then destroys an environment object (e.g. tree, bridge, rock) in the area of effect
// over DestructionTime frames after an optional StartTime delay.
//
// TARGET FACTS:
// - Registry: RW 0x64F128 / BFME2 0x64F43C ("DestroyEnvironmentUpdate", interface mask 1 = UPDATE).
// - DestroyEnvironmentUpdateModuleData:
//   createData RW 0x64F160 (0x10B) / BFME2 0x64F474 (0x10B),
//   ctor RW 0x8AFC8B / BFME2 0x4AC615 (StartTime = 0, DestructionTime = 100),
//   buildFieldParse RW 0x8AFCA4 (table RW 0xC6B844) / BFME2 0x4AC629 (table BFME2 0xC54D28):
//   * StartTime (INI::parseDurationUnsignedInt, +0x08, default 0)
//   * DestructionTime (INI::parseDurationUnsignedInt, +0x0C, default 100)
// - DestroyEnvironmentUpdate:
//   ctor RW 0x8AFCA4 / BFME2 0x4AC63A (m_startFrame = logic frame, m_objectID = 0),
//   dtor RW 0x8AFDD1 / BFME2 0x4AC767 (calls releaseEnvironmentObject),
//   releaseEnvironmentObject RW 0x8AFD8C / BFME2 0x4AC722:
//   * resolves m_objectID, calls actor->release(0.0f) and destroyObject(obj), clears m_objectID = 0.
//   findEnvironmentObject RW 0x8AFE33 / BFME2 0x4AC7C9:
//   * queries partition for KINDOF_ENVIRONMENT (0x65) whose behavior modules return non-null from slot 31 (RW 0x8AFC5F).
//   update RW 0x8AFEB5 / BFME2 0x4AC84B:
//   * without module data: UPDATE_SLEEP_FOREVER
//   * if !m_objectID, queries findEnvironmentObject(); if none found, UPDATE_SLEEP_FOREVER
//   * if elapsed < StartTime, returns UpdateSleepTime(StartTime - elapsed)
//   * if elapsed >= DestructionTime, calls releaseEnvironmentObject()
//   * else advances actor by -1.0f / DestructionTime and returns UPDATE_SLEEP_NONE
// - xfer: RW 0x8AFD35 / BFME2 0x4AC6CB (version 1, m_startFrame, m_objectID).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/EnvironmentActor.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;
class Object;

class DestroyEnvironmentUpdateModuleData : public ModuleData
{
public:
	std::uint32_t m_startTime = 0;           ///< +0x08 StartTime
	std::uint32_t m_destructionTime = 100;   ///< +0x0C DestructionTime

	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008AFCEA (getModuleName), RW 0x008AFCF0 (getModuleNameKey), RW 0x008AFE17 (scalar deleting dtor -> RW 0x008AFDD1)
class DestroyEnvironmentUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DestroyEnvironmentUpdate)

	DestroyEnvironmentUpdate(Thing *thing, const DestroyEnvironmentUpdateModuleData *data);
	~DestroyEnvironmentUpdate() override;

	UpdateSleepTime update() override;

	std::uint32_t startFrame() const { return m_startFrame; }
	ObjectID objectID() const { return m_objectID; }

	static void registerClass(ModuleFactory &modules);

private:
	void releaseEnvironmentObject();
	ObjectID findEnvironmentObject();
	EnvironmentActor *findEnvironmentActor(Object *obj);

	const DestroyEnvironmentUpdateModuleData *m_data = nullptr;
	std::uint32_t m_startFrame = 0; ///< +0x20
	ObjectID m_objectID = 0;        ///< +0x24
};
