// OpenBFME. GPL-3.0.
// Both editions: RW 0x89D758 / 0x89D7E6; BFME2 0x899FA9 / 0x89A037
// (Object/Update/SpecialEnemySenseUpdate.cpp, RVAs 0x499FA9 / 0x49A037).
// Local 171: RW 0x653E58 (SpecialEnemySenseUpdate newData), RW 0x64E165 (SpecialEnemySenseUpdate newInstance), RW 0x655009 (~SpecialEnemySenseUpdateModuleData)
#pragma once

#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

class ModuleFactory;

class SpecialEnemySenseUpdateModuleData : public ModuleData
{
public:
	ObjectFilter m_filter;
	float m_scanRange = 0.0f;
	unsigned m_scanInterval = 1;
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW SpecialEnemySenseUpdate: getModuleName RW 0x0089D70D, getModuleNameKey RW 0x0089D713,
// dtors RW 0x0089D7A9 / 0x0089D6F4, xfer RW 0x0089D7C5.
// RW 0x0089D6F4 (dtor -> RW 0x0064A492)
class SpecialEnemySenseUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(SpecialEnemySenseUpdate) // RW 0x0089D7C5
	SpecialEnemySenseUpdate(Thing *thing, const SpecialEnemySenseUpdateModuleData *data);
	UpdateSleepTime update() override;
	static void registerClass(ModuleFactory &modules);
private:
	const SpecialEnemySenseUpdateModuleData *m_data;
};
