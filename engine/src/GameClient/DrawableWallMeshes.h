// OpenBFME. GPL-3.0.
//
// DrawableWallMeshes (lane WALL-1): the walkable surfaces of a wall piece for the pathfinder, without a drawable. Retail's structure footprint routine
// (RW 0x936B7D; BFME2 0x930212) gives an object whose drawable names a WallBoundsMesh / RampMesh1 / RampMesh2 to Pathfinder::ClassifyWall (BFME2 0x92F63F),
// which asks the drawable for the meshes' world shapes (W3DScriptedModelDraw::getPolygon BFME2 RVA 0xB8440: the named sub mesh transformed by the object
// transform, its outline and z; W3DScriptedModelDraw::getRamp RVA 0xB9EC2: a Bridge from the ramp mesh). This class answers the same from the template's
// draw module data and the W3D files, like DrawableLaunchBones answers the launch bones: a headless peer gets the same bits.
//
// THE INPUTS: the first draw module of the template (its final override) that names a WallBoundsMesh, RampMesh1 or RampMesh2 (RotWK's INI puts them on the
// W3DScriptedModelDraw of every wall section, culvert, stair, gatehouse and castle wall); the model of the object's current model condition state (findBestInfo with
// the object's flags, RW 0x4B4379; lane WALL-1 took the no-flags state), posed in its bind pose (PristinePose, the simulation's numeric facade); the sub objects named by the fields (W3DFindSubObjectByName: the
// name or the part after the HLOD's dot), their triangles through their bone's pose; then the object's scale, turn and position.
//
// INFERENCE / not ported (stop S-4840 through Pathfinder::stops(); the data problems are kept in problems()):
//   * the model of the object's current model condition state (lane WALL-2; lane WALL-1 took the default state's): retail reads the drawable's current
//     render object; of several Model names the first is taken;
//   * a wall top's height is the highest z of its mesh (retail: the z of the outline's vertices; the RotWK wall meshes are flat);
//   * the meshes are used as triangles (Pathfinder tests a cell's corners against them), not as the outline polygon / Bridge retail builds.

#pragma once

#include "Common/ModelState.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameLogic/AI/AIWorld.h"

#include <map>
#include <string>
#include <vector>

class ThingTemplate;

class DrawableWallMeshes : public PathfindWallMeshSource
{
public:
	explicit DrawableWallMeshes(W3DDrawAssets &assets) : m_assets(assets) {}

	// PathfindWallMeshSource: the object's surfaces in world space
	bool wallGeometry(const Object &obj, PathfindWallGeometry &out) override;

	// The model-space surfaces of a template (bind pose, scale 1), cached by template name; null when its draw modules name no wall or ramp mesh
	struct ModelSurfaces
	{
		std::string modelName;
		std::vector<std::vector<PathfindWallTriangle>> walls, ramps, raised; // raised: lane WALL-2, RaisedWallMesh
	};
	const ModelSurfaces *surfacesOf(const ThingTemplate &tt);
	// lane WALL-2: the surfaces of the model the draw module shows for `flags` (the object's current model condition: RW 0x936B9F asks the drawable's current
	// render object, so a damaged or rubble model's meshes count), cached by template and model name
	const ModelSurfaces *surfacesOf(const ThingTemplate &tt, const ModelConditionFlags &flags);
	// `model` turned by `angle`, scaled by `scale` and moved to `pos`
	static void place(const ModelSurfaces &model, const Coord3D &pos, float angle, float scale, PathfindWallGeometry &out);

	void reset();
	const std::vector<std::string> &problems() const { return m_problems; }

private:
	struct Entry
	{
		bool has = false;
		ModelSurfaces surfaces;
	};
	void problem(const std::string &p);
	bool build(const ThingTemplate &tt, const ModelConditionFlags &flags, ModelSurfaces &out);

	W3DDrawAssets &m_assets;
	std::map<std::string, Entry> m_cache; // looked up only, never iterated
	std::vector<std::string> m_problems;
};
