// OpenBFME. GPL-3.0.
// See GameLogic/Module/ObjectHelper.h.

#include "GameLogic/Module/ObjectHelper.h"

ObjectHelperShell::ObjectHelperShell(Thing *thing, std::string helperName, std::string tag)
	: UpdateModule(thing, nullptr)
	, m_name(std::move(helperName))
	, m_tag(std::move(tag))
{
	// ZH ObjectHelper::ObjectHelper: setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER). The object is not in the list yet, so
	// friend_awakenUpdateModule just stores the frame (the module index is -1): the same state, set directly.
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
	friend_setModuleClass(m_name, NAMEKEY_INVALID);
}

UpdateSleepTime ObjectHelperShell::update()
{
	++m_calls;
	return UPDATE_SLEEP_FOREVER;
}
