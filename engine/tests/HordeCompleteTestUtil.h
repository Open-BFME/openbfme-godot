// OpenBFME unit tests. GPL-3.0.
// A test that makes an object with GameLogic::newObject completes it as RotWK's creators do once they have placed it: onBuildComplete (RW 0x68D252),
// where a horde makes its members around where it stands (HordeContain create slot 1, RW 0x8718ED; the map loop RW 0x62DCE4, the OCL RW 0x5F1623, the
// script creators RW 0x7C595A / 0x7C5B3B, production). Only a horde is completed here: the other objects keep the state these tests were written for.

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"

namespace testutil
{
inline Object *completeHorde(Object *o)
{
	if (o && o->getContain() && o->getContain()->getHordeContainInterface())
	{
		o->friend_onBuildComplete();
	}
	return o;
}
} // namespace testutil
