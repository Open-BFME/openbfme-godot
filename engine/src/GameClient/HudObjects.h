// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// What the translators ask of objects and drawables: can it be selected, is it ours, what is under the cursor, which objects lie in a screen region
// (ZH SelectionXlat.cpp CanSelectDrawable, SelectionInfo.cpp, W3DView::pickDrawable / iterateDrawablesInRegion), lane HUD-1.
//
// DEVIATIONS (stop S-284): ZH picks by W3D ray tests against the drawable's polygons; this layer tests the ray against the object's GEOMETRY (the
// template's Geometry fields: a vertical cylinder of its bounding radius and height, a box by its bounding circle), the nearest hit first, and a
// region holds the objects whose position projects into it. A pick is client state: only the id it yields reaches the logic.

#pragma once

#include "GameClient/HudContext.h"

#include <vector>

class Object;

class CommandButton;

namespace HudObjects
{
// ZH Object::isLocallyControlled
bool isLocallyControlled(const HudContext &ctx, const Object &obj);
// ZH Object::isEffectivelyDead (destroyed or no health left)
bool isEffectivelyDead(const Object &obj);
// ZH Object::isSelectable (the SELECTABLE kind of, not NO_SELECT)
bool isSelectable(const Object &obj);
// the relationship of the local player to the object's team
Relationship relationshipToLocal(const HudContext &ctx, const Object &obj);
// ZH CanSelectDrawable (SelectionXlat.cpp:81): the drawable of the object can be selected under the rules of the system. `dragSelecting`: structures and
// foreign objects are refused. A contained object (a horde member inside its horde) is refused.
bool canSelect(const HudContext &ctx, const Object &obj, bool dragSelecting);
// an object the pointer can hit: it exists, is not destroyed, is not contained
bool isPickable(const Object &obj);
// ZH View::pickDrawable(pixel, forceAttack, pickType): the nearest pickable object under the pixel, null for none. A horde member resolves to the
// horde that contains it (SelectionInfo.cpp addDrawableToList propagates a contained selection to the container).
// lane HUD-5: RotWK's pick (RW 0x48AB28): the objects whose collision type (RW 0x4B583C) meets the context's pick types are hit-tested, then a hero /
// monster is preferred, then an enemy, then the nearest (an unselectable gatehouse around a gate lets the click through to the gate).
// lane HUD-5: RotWK's callers pass their own pick types (RW 0x48AB28's third argument). pickObject(ctx, pixel) is the command translator's order click (RW 0x81FBB3 /
// 0x81FD1E: RW 0x71083F with the force attack mode); the others below are the point selection, the hover, the double click and a GUI command's object target.
Object *pickObject(const HudContext &ctx, const ICoord2D &pixel);
Object *pickObject(const HudContext &ctx, const ICoord2D &pixel, unsigned pickTypes);
// RW 0x485CB8 (W3DView::iterateDrawablesInRegion, a point region): RW 0x71083F(force attack mode) | 0x100: an own hero, then an own object, then a hero, then an enemy
Object *pickForSelection(const HudContext &ctx, const ICoord2D &pixel);
// RW 0x83CC13 (SelectionTranslator, MSG_RAW_MOUSE_POSITION): RW 0x71083F(1): FORCEATTACKABLE always
Object *pickForHover(const HudContext &ctx, const ICoord2D &pixel);
// RW 0x81F7C5 (CommandTranslator, MSG_MOUSE_LEFT_DOUBLE_CLICK): SELECTABLE only
Object *pickForDoubleClick(const HudContext &ctx, const ICoord2D &pixel);
// RW 0x83D41A .. 0x83D435 (a GUI command that needs an object target): SELECTABLE, SHRUBBERY / ROCK when the button allows them
Object *pickForGuiCommand(const HudContext &ctx, const ICoord2D &pixel, const CommandButton &command);
// the collision types (WW3D COLL_TYPE_1 .. COLL_TYPE_8 as BFME uses them: BFME2 decomp View.h, RW 0x4B583C, RW 0x48AB28)
enum PickType : unsigned
{
	PICK_TYPE_SELECTABLE = 0x04,
	PICK_TYPE_SHRUBBERY = 0x08,
	PICK_TYPE_ENEMY = 0x10,          ///< a selectable enemy of the local player
	PICK_TYPE_FORCEATTACKABLE = 0x20,
	PICK_TYPE_HERO = 0x80,           ///< a selectable HERO, MONSTER or DEPLOYED_MINE
	PICK_TYPE_OWN = 0x100,           ///< a selectable object the local player controls
	PICK_TYPE_ROCK = 0x200
};
// the collision type of the object's render object for the local player (RW 0x4B583C) and the pick types of the input context (getPickTypesForContext)
unsigned collisionTypeOf(const HudContext &ctx, const Object &obj);
unsigned pickTypesForContext(const HudContext &ctx);                   // RW 0x71083F with the force attack mode
unsigned pickTypesForContext(const HudContext &ctx, bool forceAttack); // RW 0x71083F
// the hits of the pick's cast (pickTypes | 0x194), near to far (RW 0x471A5B): what pickObject chooses from
struct PickHit
{
	float t;
	unsigned type;
	Object *obj;
};
std::vector<PickHit> pickHits(const HudContext &ctx, const ICoord2D &pixel, unsigned pickTypes);
// ZH View::iterateDrawablesInRegion: the objects whose position lies in the region, in object list order; a region of zero size is a point pick
std::vector<Object *> objectsInRegion(const HudContext &ctx, const IRegion2D &region);
// the object a contained object stands for in the selection: the container when it is the horde's member, else itself
Object *selectionRepresentative(Object &obj);
// ZH SelectionTranslator's third click test: the camera moved further than DragTolerance3D between the button down and the up (client view maths)
bool cameraMovedBeyond(const Coord3D &before, const Coord3D &after, int tolerance);
} // namespace HudObjects
