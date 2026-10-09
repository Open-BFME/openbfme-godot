// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeAIUpdate.h.

#include "GameLogic/Module/HordeAIUpdate.h"

#include "Common/GameCommon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

void HordeAIUpdate::updateBeforeBase()
{
	Object *horde = getObject();
	ContainModuleInterface *contain = horde->getContain();
	HordeContainInterface *hc = contain ? contain->getHordeContainInterface() : nullptr;
	if (!hc)
	{
		return;
	}
	// B1: `if (!isMoving() && refreshFrame < frame - 5) updateFormation(1)`
	if (!isMoving())
	{
		const unsigned frame = horde->logic().getFrame();
		const unsigned last = hc->formationRefreshFrame();
		if (frame > (unsigned)LOGICFRAMES_PER_SECOND && last < frame - (unsigned)LOGICFRAMES_PER_SECOND)
		{
			hc->updateFormation();
		}
	}
}
