// OpenBFME. GPL-3.0. See DrawableWallMeshes.h (lane WALL-1).

#include "GameClient/DrawableWallMeshes.h"

#include "GameClient/DrawableLaunchBones.h"

#include "Common/ModelState.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PristinePose.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
// the posed point: the bone's 3 x 4 matrix applied to the mesh vertex (row r: Row[r][0..2] . v + Row[r][3])
Coord3D posed(const Matrix3D &m, const W3dVectorStruct &v)
{
	using SimMath::addf32;
	using SimMath::mulf32;
	Coord3D out;
	float *dst[3] = { &out.x, &out.y, &out.z };
	for (int r = 0; r < 3; ++r)
	{
		*dst[r] = addf32(addf32(addf32(mulf32(m.Row[r][0], v.X), mulf32(m.Row[r][1], v.Y)), mulf32(m.Row[r][2], v.Z)), m.Row[r][3]);
	}
	return out;
}
} // namespace

void DrawableWallMeshes::reset()
{
	m_cache.clear();
	m_problems.clear();
}

void DrawableWallMeshes::problem(const std::string &p)
{
	if (std::find(m_problems.begin(), m_problems.end(), p) == m_problems.end())
	{
		m_problems.push_back(p);
	}
}

bool DrawableWallMeshes::build(const ThingTemplate &tt, const ModelConditionFlags &flags, ModelSurfaces &out)
{
	for (const ThingTemplate::Nugget &n : tt.drawModules().nuggets())
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
		if (!data || (data->m_wallBoundsMesh.empty() && data->m_rampMesh1.empty() && data->m_rampMesh2.empty() && data->m_raisedWallMesh.empty()))
		{
			continue;
		}
		const ModelConditionInfo *state = data->findBestInfo(flags); // RW 0x4B4379 (lane WALL-2: the object's current flags; no flags: the default state)
		// lane WALL-2: a mesh missing from another state's model (a damaged or rubble model without its wall top) is no data problem: that state has no surface
		const bool reportMissing = state == data->findBestInfo(ModelConditionFlags());
		if (!state || state->modelName().empty())
		{
			problem(tt.getName() + ": the draw module with the wall meshes has no default model");
			return false;
		}
		std::string err;
		const RenderObjPrototype *proto = m_assets.model(state->modelName(), &err);
		if (!proto)
		{
			problem(tt.getName() + ": wall model " + state->modelName() + " not found: " + err);
			return false;
		}
		if (!proto->Tree)
		{
			problem(tt.getName() + ": wall model " + state->modelName() + " has no hierarchy");
			return false;
		}
		std::vector<Matrix3D> pose;
		if (!PristinePose::evaluate(*proto->Tree, nullptr, 0, 1.0f, pose, &err))
		{
			problem(tt.getName() + ": the bind pose of " + state->modelName() + ": " + err);
			return false;
		}
		out.modelName = state->modelName();
		auto meshTriangles = [&](const std::string &name, std::vector<std::vector<PathfindWallTriangle>> &into) {
			if (name.empty())
			{
				return;
			}
			const int idx = W3DFindSubObjectByName(*proto, name);
			const RenderSubObject *sub = idx >= 0 ? &proto->SubObjects[(size_t)idx] : nullptr;
			if (!sub || !sub->Mesh || sub->BoneIndex < 0 || sub->BoneIndex >= (int)pose.size())
			{
				if (reportMissing)
				{
					problem(tt.getName() + ": mesh " + name + " is not a mesh of " + state->modelName());
				}
				return;
			}
			const MeshModelClass &mesh = *sub->Mesh;
			const Matrix3D &bone = pose[(size_t)sub->BoneIndex];
			std::vector<PathfindWallTriangle> tris;
			for (const W3dTriStruct &t : mesh.Triangles)
			{
				PathfindWallTriangle w;
				bool ok = true;
				for (int k = 0; k < 3; ++k)
				{
					if (t.Vindex[k] >= mesh.Vertices.size())
					{
						ok = false;
						break;
					}
					w.v[k] = posed(bone, mesh.Vertices[t.Vindex[k]]);
				}
				if (ok)
				{
					tris.push_back(w);
				}
			}
			if (tris.empty())
			{
				problem(tt.getName() + ": mesh " + name + " of " + state->modelName() + " has no triangles");
				return;
			}
			into.push_back(std::move(tris));
		};
		meshTriangles(data->m_wallBoundsMesh, out.walls);
		meshTriangles(data->m_rampMesh1, out.ramps);
		meshTriangles(data->m_rampMesh2, out.ramps);
		meshTriangles(data->m_raisedWallMesh, out.raised); // lane WALL-2: W3DModelDraw data + 0x110 (RW 0xBE1520), the drawable's vslot 0x9C (RW 0x67290D)
		return !out.walls.empty() || !out.ramps.empty() || !out.raised.empty();
	}
	return false;
}

const DrawableWallMeshes::ModelSurfaces *DrawableWallMeshes::surfacesOf(const ThingTemplate &tt)
{
	return surfacesOf(tt, ModelConditionFlags());
}

const DrawableWallMeshes::ModelSurfaces *DrawableWallMeshes::surfacesOf(const ThingTemplate &tt, const ModelConditionFlags &flags)
{
	// the key: the template and the model the flags select (the first draw module naming a wall mesh)
	std::string key = tt.getName();
	for (const ThingTemplate::Nugget &n : tt.drawModules().nuggets())
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
		if (data && !(data->m_wallBoundsMesh.empty() && data->m_rampMesh1.empty() && data->m_rampMesh2.empty() && data->m_raisedWallMesh.empty()))
		{
			const ModelConditionInfo *state = data->findBestInfo(flags);
			key += "|" + (state ? state->modelName() : std::string());
			break;
		}
	}
	auto it = m_cache.find(key);
	if (it == m_cache.end())
	{
		Entry e;
		e.has = build(tt, flags, e.surfaces);
		it = m_cache.emplace(key, std::move(e)).first;
	}
	return it->second.has ? &it->second.surfaces : nullptr;
}

void DrawableWallMeshes::place(const ModelSurfaces &model, const Coord3D &pos, float angle, float scale, PathfindWallGeometry &out)
{
	using SimMath::addf32;
	using SimMath::mulf32;
	using SimMath::subf32;
	const float c = SimMath::cosDet(angle), s = SimMath::sinDet(angle);
	auto convert = [&](const std::vector<PathfindWallTriangle> &tris, std::vector<PathfindWallSurface> &into) {
		PathfindWallSurface surf;
		bool first = true;
		for (const PathfindWallTriangle &t : tris)
		{
			PathfindWallTriangle w;
			for (int k = 0; k < 3; ++k)
			{
				const float lx = mulf32(t.v[k].x, scale), ly = mulf32(t.v[k].y, scale), lz = mulf32(t.v[k].z, scale);
				Coord3D &p = w.v[k];
				p.x = addf32(pos.x, subf32(mulf32(lx, c), mulf32(ly, s)));
				p.y = addf32(pos.y, addf32(mulf32(lx, s), mulf32(ly, c)));
				p.z = addf32(pos.z, lz);
				if (first)
				{
					surf.loX = surf.hiX = p.x;
					surf.loY = surf.hiY = p.y;
					surf.height = p.z;
					first = false;
				}
				surf.loX = std::min(surf.loX, p.x);
				surf.loY = std::min(surf.loY, p.y);
				surf.hiX = std::max(surf.hiX, p.x);
				surf.hiY = std::max(surf.hiY, p.y);
				surf.height = std::max(surf.height, p.z);
			}
			surf.triangles.push_back(w);
		}
		into.push_back(std::move(surf));
	};
	for (const auto &w : model.walls)
	{
		convert(w, out.walls);
	}
	for (const auto &r : model.ramps)
	{
		convert(r, out.ramps);
	}
	for (const auto &r : model.raised)
	{
		convert(r, out.raised);
	}
}

bool DrawableWallMeshes::wallGeometry(const Object &obj, PathfindWallGeometry &out)
{
	const ThingTemplate *tt = obj.getTemplate();
	if (!tt)
	{
		return false;
	}
	const ModelSurfaces *model = surfacesOf(*tt->getFinalOverride(), DrawableLaunchBones::launchFlags(obj)); // lane WALL-2: the object's current model
	if (!model)
	{
		return false;
	}
	place(*model, *obj.getPosition(), obj.getOrientation(), obj.getInstanceScale(), out);
	return true;
}
