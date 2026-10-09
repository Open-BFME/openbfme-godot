// OpenBFME. GPL-3.0.
//
// The W3D draw module classes of the RotWK registry (module type DRAW, 20 classes, golden module-registry.json) and what this
// engine does with each of them at map start (lane MAPOBJ-1):
//
//   * ModelDraw  - drawn through the merged W3DScriptedModelDraw runtime (the model of the state matching the object's model
//                  condition flags and its idle animation): W3DScriptedModelDraw, W3DHordeModelDraw, W3DTruckDraw, W3DSailModelDraw,
//                  W3DQuadrupedDraw, W3DTankDraw, W3DSupplyDraw. The variants' own motion (tires, treads, sail, feet) is S-112.
//   * TreeDraw / PropDraw / FloorDraw - static models (W3DTreeDraw.h). Dynamic tree behaviour is S-111.
//   * W3DDefaultDraw - a retail release build draws nothing (ZH W3DDefaultDraw.cpp: its render object exists only under
//                  LOAD_TEST_ASSETS, and the retail INI bodies are empty).
//   * the others (Light, Streak, Buff, Tornado, Laser, Debris, ProjectileStream, BoatWake, Rope) are effects driven by game state
//                  (a projectile, a buff, a death) and are reported as NOT DRAWN, never silently skipped (stop S-113).
//
// registerTypedDrawModuleData binds every class that has typed module data (ModuleFactory::bindTypedData<T>); the remaining
// draw classes keep the factory's RawModuleData (their bodies are consumed to their End by the binary's own tables).

#pragma once

#include "Common/Thing/ModuleFactory.h"

#include <string>
#include <vector>

enum W3DDrawKind
{
	W3D_DRAWKIND_MODEL = 0,   ///< a W3DModelDrawModuleData descendant: drawn by the draw module runtime
	W3D_DRAWKIND_TREE,        ///< W3DTreeDraw
	W3D_DRAWKIND_PROP,        ///< W3DPropDraw
	W3D_DRAWKIND_FLOOR,       ///< W3DFloorDraw
	W3D_DRAWKIND_NOTHING,     ///< W3DDefaultDraw: draws nothing in a release build
	W3D_DRAWKIND_NOT_DRAWN    ///< an effect draw this engine does not draw (S-113)
};

struct W3DDrawClassInfo
{
	const char *name;
	W3DDrawKind kind;
};

namespace W3DDrawModules
{
// Every DRAW class of the registry, in registry order.
const std::vector<W3DDrawClassInfo> &all();
// nullptr for a class that is not a draw class (the caller reports it).
const W3DDrawClassInfo *find(const std::string &className);
// Binds the typed module data classes (Scripted, Horde, Default, Tree, Prop, Floor, Truck, Sail, Quadruped, Tank, Supply) into the
// factory. The classes must be registered (modules.init() first).
void registerTypedDrawModuleData(ModuleFactory &modules);
// The draw classes that stay raw / are not drawn, for reports: "Class: reason".
std::vector<std::string> notDrawnClasses();
} // namespace W3DDrawModules
