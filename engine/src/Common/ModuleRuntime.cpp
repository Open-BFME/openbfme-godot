// OpenBFME. GPL-3.0.
// ObjectModule and DrawableModule constructors (the runtime half of Common/Module.h).

#include "Common/Module.h"

#include "Common/Thing/Thing.h"

ObjectModule::ObjectModule(Thing *thing, const ModuleData *moduleData)
	: Module(moduleData)
	, m_object(thing ? thing->asObject() : nullptr)
{
}

DrawableModule::DrawableModule(Thing *thing, const ModuleData *moduleData)
	: Module(moduleData)
	, m_drawable(thing ? thing->asDrawable() : nullptr)
{
}
