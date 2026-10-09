// OpenBFME. GPL-3.0.
// See GameLogic/Module/UnportedModule.h.

#include "GameLogic/Module/UnportedModule.h"

UnportedUpdateModule::UnportedUpdateModule(Thing *thing, const ModuleData *data, int interfaceMask)
	: UpdateModule(thing, data)
	, m_mask(interfaceMask)
{
	// the module is not ported: it has nothing to run, so it starts asleep (the way ObjectHelper's constructor starts: ZH ObjectHelper.h)
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
}

UpdateSleepTime UnportedUpdateModule::update()
{
	++m_calls;
	return UPDATE_SLEEP_FOREVER;
}

std::unique_ptr<Module> makeUnportedModule(Thing *thing, const ModuleData *data, const std::string &className, ModuleType type, int interfaceMask)
{
	(void)className;
	if (type == MODULETYPE_BEHAVIOR)
	{
		if (interfaceMask & MODULEINTERFACE_UPDATE)
		{
			return std::make_unique<UnportedUpdateModule>(thing, data, interfaceMask);
		}
		return std::make_unique<UnportedBehaviorModule>(thing, data, interfaceMask);
	}
	return std::make_unique<UnportedDrawableModule>(thing, data, type, interfaceMask);
}
