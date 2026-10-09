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
Object *pickObject(const HudContext &ctx, const ICoord2D &pixel);
// ZH View::iterateDrawablesInRegion: the objects whose position lies in the region, in object list order; a region of zero size is a point pick
std::vector<Object *> objectsInRegion(const HudContext &ctx, const IRegion2D &region);
// the object a contained object stands for in the selection: the container when it is the horde's member, else itself
Object *selectionRepresentative(Object &obj);
// ZH SelectionTranslator's third click test: the camera moved further than DragTolerance3D between the button down and the up (client view maths)
bool cameraMovedBeyond(const Coord3D &before, const Coord3D &after, int tolerance);
} // namespace HudObjects
