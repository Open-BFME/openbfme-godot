// OpenBFME. GPL-3.0. See DrawableScriptTarget.h (lane FX-3). Client only: the answers feed draw scripts, never the simulation.

#include "GameClient/DrawableScriptTarget.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

DrawableScriptTarget DrawableScriptTarget::capture(const GameLogic &logic, const Object &obj)
{
	DrawableScriptTarget t;
	t.hasObject = true;
	t.ownPosition = *obj.getPosition();
	t.ownAngle = obj.getOrientation();
	if (const ObjectWeapons *w = obj.getWeapons())
	{
		t.targetPosition = w->drawTargetPosition();
		if (const Object *victim = w->drawTargetID() != INVALID_ID ? logic.findObjectByID(w->drawTargetID()) : nullptr)
		{
			t.targetExists = true;
			t.targetKindOf = victim->getKindOf();
		}
	}
	return t;
}

bool DrawableScriptTarget::isTargetKindOf(const std::string &kindName) const
{
	if (!hasObject || !targetExists)
	{
		return false;
	}
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kindName); // RW 0x6AAD1A
	return bit >= 0 && MaskTest(targetKindOf, (unsigned)bit);
}

float DrawableScriptTarget::bearing() const
{
	// RW 0x4B3D8D
	float dx = targetPosition.x - ownPosition.x;
	float dy = targetPosition.y - ownPosition.y;
	const float len = (float)std::sqrt((double)(dx * dx + dy * dy));
	float angle = 0.0f;
	if (len != 0.0f)
	{
		const float inv = 1.0f / len;
		dx = inv * dx;
		dy = inv * dy;
		const float dirX = std::cos(ownAngle);
		const float dirY = std::sin(ownAngle);
		float dot = dirX * dx + dirY * dy;
		if ((double)dot < -1.0)
		{
			dot = -1.0f;
		}
		else if (dot > 1.0f)
		{
			dot = 1.0f;
		}
		angle = std::acos(dot);
		if (dirX * dy - dirY * dx < 0.0f)
		{
			angle = 0.0f - angle;
		}
	}
	// RW 0x644FD0
	const float pi = 3.14159274f, twoPi = 6.28318548f;
	while (angle > pi)
	{
		angle -= twoPi;
	}
	while (angle <= -pi)
	{
		angle += twoPi;
	}
	return angle;
}
