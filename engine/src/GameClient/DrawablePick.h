// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane QA-1: the HUD's pick ray against what a drawable shows.
//
// DONOR FACTS (ZH W3DView::pickDrawable, RTS3DScene::castRay, HLodClass / MeshClass::Cast_Ray): the pick casts the ray at the scene's render objects and
// takes the drawable from the nearest render object it hits (its user data). A render object is the drawn W3D model of a draw module: the meshes of the
// HLOD's current LOD and its aggregates, at the drawable's transform; a drawable that draws no render object (a castle shell with Model = None, a
// W3DDefaultDraw helper, a tree or prop of the batched buffers) cannot be picked. community PR #36: with BFME1 mounted, W3DFloorDraw models (its build plots) are tested too.
// DATA FACT (RotWK): structure models are far larger than their GEOMETRY volumes (GondorBarracks: a cylinder of radius 8 and height 10 under a building
// about 60 across), and a fortress shell (MenFortress) stands on the spot of its citadel; the geometry pick of S-284 selected the wrong object for both.
// TARGET FACTS (lane SELECT-1): RotWK's pick (RW 0x48AB28) casts at the scene (RW 0x471A5B) with the ray test's +0x42 flag set; HLodClass::Cast_Ray (RW
// 0x59C680) then lets the first top-LOD OBBoxRenderObjClass child (class 0x1B, Cast_Ray RW 0x575220) answer alone, and HLod::Set_Collision_Type (RW
// 0x53B5C0) gives that box the draw's collision type. Every RotWK soldier sampled carries such a box (its BOUNDINGBOX), so a soldier is hit by its box.
// A model without an oriented box is tested by its triangles: the highest LOD and aggregates, every visible mesh (W3D_MESH_FLAG_HIDDEN and
// script-hidden sub objects are skipped), both faces. A box on an animated bone is posed with the draw's track-0 animation frame; NOT READ (stop S-1200):
// animation blending for it, the triangles' animated pose (they stand on the bind pose).
//
// Client state only: nothing here reaches the simulation (the pick yields an object id that the selection translator turns into a message).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstddef>

class Drawable;

namespace DrawablePick
{
enum class Result
{
	NotDrawn, ///< the drawable shows no model (nothing of it is in the scene): it cannot be picked
	Miss,
	Hit,
	Unknown   ///< a model draw that has not chosen its model yet: the caller decides (the geometry pick)
};

// The ray o + d * t (SAGE world space, t >= 0) against the shown models of `drawable`; on Hit, *t is the nearest hit.
Result rayTest(const Drawable &drawable, const Coord3D &o, const Coord3D &d, float *t);

// The cached model-space triangles of the prototypes seen so far (tests, reports).
size_t cachedModels();

// "[S-1200] ...": what this pick does not take from the target (InGameHud::stops)
const char *stopLine();
} // namespace DrawablePick
