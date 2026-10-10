// OpenBFME. GPL-3.0.
// See HudObjects.h.

#include "GameClient/HudObjects.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <algorithm>
#include <cmath>

namespace ModelCondition
{
int indexOf(const std::string &name);
}

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

unsigned collisionTypeOf(const HudContext &ctx, const Object &obj)
{
	// lane HUD-5: the render object's collision type as RotWK's model draw sets it (RW 0x4B583C, tier B of BFME2 0x4B5FE6; Set_Collision_Type replaces the
	// type): SELECTABLE or WALK_ON_TOP_OF_WALL -> 4 (the draw module's own override, W3DModelDraw + 0x214, is not ported: S-1954); SHRUBBERY -> 8, else ROCK or
	// ROCK_VENDOR -> 0x200; a selectable one becomes 0x84 for HERO, MONSTER or DEPLOYED_MINE, | 0x100 when locally controlled (RW 0x68B749), else | 0x10 for an
	// enemy of the local player (RW 0x6ADBEB) that is not WALK_ON_TOP_OF_WALL; FORCEATTACKABLE -> 0x20; CLICK_THROUGH -> 0; then (neither BRIDGE nor
	// BRIDGE_TOWER) a STRUCTURE's rubble or a dead object -> 0 (the object status bits RW 0x4B5988 / 0x4B59CE are taken as "effectively dead": S-1954)
	unsigned t = 0;
	if (obj.isKindOfName("SELECTABLE") || obj.isKindOfName("WALK_ON_TOP_OF_WALL"))
	{
		t = PICK_TYPE_SELECTABLE;
	}
	if (obj.isKindOfName("SHRUBBERY"))
	{
		t = PICK_TYPE_SHRUBBERY;
	}
	else if (obj.isKindOfName("ROCK") || obj.isKindOfName("ROCK_VENDOR"))
	{
		t = PICK_TYPE_ROCK;
	}
	if (t & PICK_TYPE_SELECTABLE)
	{
		unsigned f = PICK_TYPE_SELECTABLE;
		if (obj.isKindOfName("HERO") || obj.isKindOfName("MONSTER") || obj.isKindOfName("DEPLOYED_MINE"))
		{
			f = PICK_TYPE_SELECTABLE | PICK_TYPE_HERO;
		}
		if (isLocallyControlled(ctx, obj))
		{
			f |= PICK_TYPE_OWN;
		}
		else if (relationshipToLocal(ctx, obj) == ENEMIES && !obj.isKindOfName("WALK_ON_TOP_OF_WALL"))
		{
			f |= PICK_TYPE_ENEMY;
		}
		t = f;
	}
	if (obj.isKindOfName("FORCEATTACKABLE"))
	{
		t = PICK_TYPE_FORCEATTACKABLE;
	}
	if (obj.isKindOfName("CLICK_THROUGH"))
	{
		t = 0;
	}
	if (!obj.isKindOfName("BRIDGE") && !obj.isKindOfName("BRIDGE_TOWER"))
	{
		static const int RUBBLE = ModelCondition::indexOf("RUBBLE");
		if ((obj.isKindOfName("STRUCTURE") && obj.testModelCondition(RUBBLE)) || isEffectivelyDead(obj))
		{
			t = 0;
		}
	}
	return t;
}

namespace
{
// RW 0x68B4C6(6): the object's weapon set deals FLAME damage (Object + 0x37C, the damage type mask of the set: ZH WeaponSet::m_totalDamageTypeFlags; here the
// DamageType of the live slots' templates)
bool dealsFlame(const Object &obj)
{
	const ObjectWeapons *w = obj.getWeapons();
	for (int i = 0; w && i < WEAPONSLOT_COUNT; ++i)
	{
		const Weapon *weapon = w->weaponInSlot(i);
		if (weapon && weapon->getTemplate() && weapon->getTemplate()->m_damageType == DAMAGE_FLAME)
		{
			return true;
		}
	}
	return false;
}

// RW 0x691F7F / 0x691FCE: a button of the object's command set (RW 0x69156B -> ControlBar RW 0x71EFA2, its 33 slots) with the option
bool commandSetHasOption(const HudContext &ctx, const Object &obj, std::uint32_t option)
{
	const CommandSet *set = ctx.commands ? ctx.commands->findCommandSet(obj.getCommandSetName()) : nullptr;
	for (int i = 0; set && i < (int)CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && b->hasOption(option))
		{
			return true;
		}
	}
	return false;
}
} // namespace

unsigned pickTypesForContext(const HudContext &ctx)
{
	return pickTypesForContext(ctx, ctx.ui.isInForceAttackMode());
}

unsigned pickTypesForContext(const HudContext &ctx, bool forceAttack)
{
	// lane HUD-5: RW 0x71083F (BFME2 getPickTypesForContext): 0x44, 0x64 with forceAttack (FORCEATTACKABLE); the GUI command's ALLOW_SHRUBBERY_TARGET -> 8 and
	// ALLOW_ROCK_TARGET -> 0x200; without a GUI command RW 0x71077B: when the selection is controllable (RW 0x69D1AD), the first selected object (in order) that
	// deals FLAME damage or has a SHRUBBERY button (RW 0x691F7F) adds 8, a ROCK button (RW 0x691FCE) 0x200, and the scan stops (`test bp, 0x208`); RotWK does not
	// gate the flame on the force attack mode. The 0x40 bit (a collision type the drawable's + 0x457 & 0x10 sets, RW 0x4B5988) has no object here (S-1954)
	unsigned types = PICK_TYPE_SELECTABLE;
	if (forceAttack)
	{
		types |= PICK_TYPE_FORCEATTACKABLE;
	}
	if (const CommandButton *command = ctx.ui.getGUICommand())
	{
		if (command->hasOption(COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET))
		{
			types |= PICK_TYPE_SHRUBBERY;
		}
		if (command->hasOption(COMMAND_OPTION_ALLOW_ROCK_TARGET))
		{
			types |= PICK_TYPE_ROCK;
		}
		return types;
	}
	const Object *first = ctx.logic.findObjectByID(ctx.ui.firstSelected());
	if (!first || first->isDestroyed() || !isLocallyControlled(ctx, *first))
	{
		return types;
	}
	unsigned extra = 0;
	for (ObjectID id : ctx.ui.selected())
	{
		const Object *obj = ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		if (dealsFlame(*obj) || commandSetHasOption(ctx, *obj, COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET))
		{
			extra |= PICK_TYPE_SHRUBBERY;
		}
		if (commandSetHasOption(ctx, *obj, COMMAND_OPTION_ALLOW_ROCK_TARGET))
		{
			extra |= PICK_TYPE_ROCK;
		}
		if (extra)
		{
			break;
		}
	}
	return types | extra;
}

std::vector<PickHit> pickHits(const HudContext &ctx, const ICoord2D &pixel, unsigned pickTypes)
{
	std::vector<PickHit> hits;
	Coord3D o, d;
	if (!ctx.view.screenToRay(pixel, o, d))
	{
		return hits;
	}
	const unsigned castMask = pickTypes | PICK_TYPE_SELECTABLE | PICK_TYPE_ENEMY | PICK_TYPE_HERO | PICK_TYPE_OWN;
	for (Object *obj = ctx.logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!isPickable(*obj) || InvisibilityManager::clientLook(*obj, ctx.localPlayer()) == 5) // lane STEALTH-1: an enemy's invisible object is not drawn, not picked
		{
			continue;
		}
		const unsigned type = collisionTypeOf(ctx, *obj);
		if ((type & castMask) == 0)
		{
			continue; // RW 0x471ABD: a render object whose collision type is not in the cast's mask is not tested
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
		if (hit)
		{
			hits.push_back({ t, type, obj });
		}
	}
	std::stable_sort(hits.begin(), hits.end(), [](const PickHit &a, const PickHit &b) { return a.t < b.t; });
	return hits;
}

Object *pickObject(const HudContext &ctx, const ICoord2D &pixel, unsigned pickTypes)
{
	// lane HUD-5: W3DView::pickDrawable (RW 0x48AB28, tier A): every render object whose collision type meets pickType | 0x194 is hit-tested (the all-hits
	// RTS3DScene cast RW 0x471A5B, sorted near to far); then the nearest hit whose type has all of 0x184, else all of 0x104 (only when pickType has 0x100), else
	// all of 0x84 (a hero / monster), else all of 0x14 (an enemy), else any bit of pickType (RW 0x489BE6 picks the first match)
	const std::vector<PickHit> hits = pickHits(ctx, pixel, pickTypes);
	auto first = [&](unsigned mask, bool all) -> Object * {
		for (const PickHit &h : hits)
		{
			if (all ? (h.type & mask) == mask : (h.type & mask) != 0)
			{
				return h.obj;
			}
		}
		return nullptr;
	};
	Object *best = nullptr;
	if (pickTypes & PICK_TYPE_OWN)
	{
		best = first(PICK_TYPE_OWN | PICK_TYPE_HERO | PICK_TYPE_SELECTABLE, true);
		best = best ? best : first(PICK_TYPE_OWN | PICK_TYPE_SELECTABLE, true);
	}
	best = best ? best : first(PICK_TYPE_HERO | PICK_TYPE_SELECTABLE, true);
	best = best ? best : first(PICK_TYPE_ENEMY | PICK_TYPE_SELECTABLE, true);
	best = best ? best : first(pickTypes, false);
	return best ? selectionRepresentative(*best) : nullptr;
}

Object *pickObject(const HudContext &ctx, const ICoord2D &pixel)
{
	return pickObject(ctx, pixel, pickTypesForContext(ctx));
}

Object *pickForSelection(const HudContext &ctx, const ICoord2D &pixel)
{
	return pickObject(ctx, pixel, pickTypesForContext(ctx) | PICK_TYPE_OWN); // RW 0x485CB1 .. 0x485CB8: `or ah, 1`
}

Object *pickForHover(const HudContext &ctx, const ICoord2D &pixel)
{
	return pickObject(ctx, pixel, pickTypesForContext(ctx, true)); // RW 0x83CC13: push 1 -> RW 0x71083F
}

Object *pickForDoubleClick(const HudContext &ctx, const ICoord2D &pixel)
{
	return pickObject(ctx, pixel, PICK_TYPE_SELECTABLE); // RW 0x81F7C5: push 4
}

Object *pickForGuiCommand(const HudContext &ctx, const ICoord2D &pixel, const CommandButton &command)
{
	unsigned types = PICK_TYPE_SELECTABLE; // RW 0x83D41E .. 0x83D432
	if (command.hasOption(COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET))
	{
		types |= PICK_TYPE_SHRUBBERY;
	}
	if (command.hasOption(COMMAND_OPTION_ALLOW_ROCK_TARGET))
	{
		types |= PICK_TYPE_ROCK;
	}
	return pickObject(ctx, pixel, types);
}

std::vector<Object *> objectsInRegion(const HudContext &ctx, const IRegion2D &region)
{
	std::vector<Object *> out;
	const bool point = region.lo.x == region.hi.x && region.lo.y == region.hi.y;
	if (point)
	{
		if (Object *o = pickForSelection(ctx, region.lo)) // RW 0x485CB8
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
