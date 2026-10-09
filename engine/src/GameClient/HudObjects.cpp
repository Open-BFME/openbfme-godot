// OpenBFME. GPL-3.0.
// See HudObjects.h.

#include "GameClient/HudObjects.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <algorithm>
#include <cmath>

namespace HudObjects
{
namespace
{
// ZH SelectionXlat.cpp CanSelectDrawable also tests the window under the drawable (the GUI obscures it): the window layer has no say in the
// world pick here; the HUD refuses a world pick under its own windows before the translators run (HudInput)

struct Shape
{
	float radius = 10.0f;
	float height = 10.0f;
};

Shape shapeOf(const HudContext &ctx, const Object &obj)
{
	Shape s;
	if (ctx.ai && obj.getTemplate())
	{
		const ObjectMovementInfo &info = ctx.ai->movementInfo(*obj.getTemplate());
		if (info.hasGeometry)
		{
			s.radius = std::max(info.geometry.boundingCircleRadius(), 1.0f);
			s.height = std::max(info.geometry.height, s.radius);
		}
	}
	return s;
}

// nearest hit parameter of the ray with the vertical cylinder; false for a miss
bool rayCylinder(const Coord3D &o, const Coord3D &d, const Coord3D &base, float radius, float height, float &tHit)
{
	bool hit = false;
	float best = 0.0f;
	auto consider = [&](float t) {
		if (t >= 0.0f && (!hit || t < best))
		{
			hit = true;
			best = t;
		}
	};
	const float ox = o.x - base.x, oy = o.y - base.y;
	const float a = d.x * d.x + d.y * d.y;
	if (a > 1e-12f)
	{
		const float b = 2.0f * (ox * d.x + oy * d.y);
		const float c = ox * ox + oy * oy - radius * radius;
		const float disc = b * b - 4.0f * a * c;
		if (disc >= 0.0f)
		{
			const float sq = std::sqrt(disc);
			for (float t : { (-b - sq) / (2.0f * a), (-b + sq) / (2.0f * a) })
			{
				const float z = o.z + d.z * t - base.z;
				if (z >= 0.0f && z <= height)
				{
					consider(t);
				}
			}
		}
	}
	if (std::fabs(d.z) > 1e-9f)
	{
		for (float planeZ : { base.z, base.z + height })
		{
			const float t = (planeZ - o.z) / d.z;
			const float x = o.x + d.x * t - base.x, y = o.y + d.y * t - base.y;
			if (x * x + y * y <= radius * radius)
			{
				consider(t);
			}
		}
	}
	tHit = best;
	return hit;
}
} // namespace

bool isLocallyControlled(const HudContext &ctx, const Object &obj)
{
	return obj.getControllingPlayer() != nullptr && obj.getControllingPlayer() == ctx.localPlayer();
}

bool isEffectivelyDead(const Object &obj)
{
	if (obj.isDestroyed())
	{
		return true;
	}
	if (const BodyModuleInterface *body = obj.getBodyModule())
	{
		return body->getMaxHealth() > 0.0f && body->getHealth() <= 0.0f;
	}
	return false;
}

bool isSelectable(const Object &obj)
{
	return obj.isKindOfName("SELECTABLE") && !obj.isKindOfName("NO_SELECT") && obj.isScriptSelectable(); // lane SCRIPT-2: RW + 0x454
}

Relationship relationshipToLocal(const HudContext &ctx, const Object &obj)
{
	const Player *local = ctx.localPlayer();
	if (!local || !obj.getTeam())
	{
		return NEUTRAL;
	}
	return local->getRelationship(obj.getTeam());
}

bool isPickable(const Object &obj)
{
	return !obj.isDestroyed();
}

Object *selectionRepresentative(Object &obj)
{
	// ZH addDrawableToList (SelectionInfo.cpp): a contained object that is not selectable passes the pick to its container. lane QA-1: a soldier of a horde
	// passes it too, SELECTABLE or not (GondorFighter is): the horde itself draws no model (GondorFighterHorde: Model = None), so the drawn-model pick
	// (S-1200) only ever meets its soldiers, and BFME's SelectionXlat refuses a contained object ("we're contained, and so we shouldn't be selectable",
	// Open-BFME-1 SelectionXlat.cpp); RotWK's own route from the member's drawable to its horde is not read (inference)
	Object *o = &obj;
	while (o->getContainedBy() && (!isSelectable(*o) || o->getContainedBy()->isKindOfName("HORDE")))
	{
		o = o->getContainedBy();
	}
	return o;
}

bool canSelect(const HudContext &ctx, const Object &obj, bool dragSelecting)
{
	// ZH CanSelectDrawableZeroHourReference, in its order
	if (isEffectivelyDead(obj) && !obj.isKindOfName("ALWAYS_SELECTABLE"))
	{
		return false;
	}
	if (!obj.isKindOfName("SELECTABLE") && obj.isKindOfName("FORCEATTACKABLE"))
	{
		return false;
	}
	// structures cannot be selected by a drag select, you must individually pick them
	if (dragSelecting && obj.isKindOfName("STRUCTURE"))
	{
		return false;
	}
	if (!isSelectable(obj))
	{
		return false;
	}
	// only your own objects can be drag selected
	if (dragSelecting && !isLocallyControlled(ctx, obj))
	{
		return false;
	}
	return true;
}

Object *pickObject(const HudContext &ctx, const ICoord2D &pixel)
{
	Coord3D o, d;
	if (!ctx.view.screenToRay(pixel, o, d))
	{
		return nullptr;
	}
	Object *best = nullptr;
	float bestT = 0.0f;
	for (Object *obj = ctx.logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!isPickable(*obj) || InvisibilityManager::clientLook(*obj, ctx.localPlayer()) == 5) // lane STEALTH-1: an enemy's invisible object is not drawn, not picked
		{
			continue;
		}
		float t = 0.0f;
		bool hit = false;
		const DrawablePick::Result drawn = ctx.pickRay ? ctx.pickRay(*obj, o, d, &t) : DrawablePick::Result::Unknown;
		if (drawn == DrawablePick::Result::Hit)
		{
			hit = true; // lane QA-1: the ray met the drawn model (ZH W3DView::pickDrawable)
		}
		else if (drawn == DrawablePick::Result::Unknown)
		{
			const Shape s = shapeOf(ctx, *obj);
			hit = rayCylinder(o, d, *obj->getPosition(), s.radius, s.height, t);
		}
		if (hit && (!best || t < bestT))
		{
			best = obj;
			bestT = t;
		}
	}
	return best ? selectionRepresentative(*best) : nullptr;
}

std::vector<Object *> objectsInRegion(const HudContext &ctx, const IRegion2D &region)
{
	std::vector<Object *> out;
	const bool point = region.lo.x == region.hi.x && region.lo.y == region.hi.y;
	if (point)
	{
		if (Object *o = pickObject(ctx, region.lo))
		{
			out.push_back(o);
		}
		return out;
	}
	for (Object *obj = ctx.logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!isPickable(*obj) || obj->getContainedBy() || InvisibilityManager::clientLook(*obj, ctx.localPlayer()) == 5) // lane STEALTH-1
		{
			continue;
		}
		ICoord2D px;
		if (!ctx.view.worldToScreen(*obj->getPosition(), px))
		{
			continue;
		}
		if (px.x >= region.lo.x && px.x <= region.hi.x && px.y >= region.lo.y && px.y <= region.hi.y)
		{
			out.push_back(obj);
		}
	}
	return out;
}

bool cameraMovedBeyond(const Coord3D &before, const Coord3D &after, int tolerance)
{
	const float dx = after.x - before.x, dy = after.y - before.y, dz = after.z - before.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz) > (float)tolerance;
}

} // namespace HudObjects
