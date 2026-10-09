// OpenBFME. GPL-3.0.
// ThingFactory::newObject (lane LOGIC-1). See GameLogic/Object/Object.h for the creation order and the RotWK addresses.

#include "Common/Thing/ThingFactory.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

Object *ThingFactory::newObject(GameLogic &logic, const ThingTemplate *tt, Team *team, const ObjectStatusMaskType &status, ObjectID id)
{
	if (!tt)
	{
		return nullptr; // RW 0x6D1669: a null template makes nothing
	}
	// step 1, RW 0x6D168B .. 0x6D16C4: BuildVariations of the template as passed (tt + 0x330, a name list): one is picked with the logic
	// RNG (file ThingFactory.cpp line 0x20B) and replaces the template when its name resolves (RW 0x6D1305)
	const ThingTemplate *use = tt;
	const std::vector<std::string> &variations = logic.templateInfo(tt).buildVariations;
	if (!variations.empty())
	{
		const int pick = logic.random().getValue(0, (int)variations.size() - 1, "ThingFactory.cpp", 0x20B);
		if (const ThingTemplate *variation = findTemplate(variations[(size_t)pick]))
		{
			use = variation;
		}
	}
	// step 2: the constructor (helpers, behaviors, onObjectCreated, registerObject)
	Object *obj = new Object(logic, use, status, team, id);
	// step 3: CreateModule::onCreate, step 4: initObject
	obj->friend_runCreateModules();
	obj->friend_initObject(); // RW 0x693D0C -> sendObjectCreated (RW 0x628882): the creation draw, the drawable, OnCreated
	return obj;
}
