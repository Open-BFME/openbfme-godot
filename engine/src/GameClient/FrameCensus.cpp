// OpenBFME. GPL-3.0.
// See GameClient/FrameCensus.h.

#include "GameClient/FrameCensus.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

FrameCensus::Counts FrameCensus::count(const GameLogic &logic)
{
	const int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const int infantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY");
	const int cavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	const int monster = ObjectTemplateInfoBuilder::kindOfIndex("MONSTER");
	auto is = [](const Object &o, int bit) { return bit >= 0 && o.isKindOf((unsigned)bit); };
	Counts c;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		++c.objects;
		if (o->isEffectivelyDead())
		{
			continue;
		}
		c.battalions += is(*o, horde) ? 1 : 0;
		c.troops += is(*o, infantry) || is(*o, cavalry) || is(*o, monster) ? 1 : 0;
	}
	return c;
}
