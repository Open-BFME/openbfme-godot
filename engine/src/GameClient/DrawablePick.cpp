// OpenBFME. GPL-3.0.
// See DrawablePick.h.

#include "GameClient/DrawablePick.h"
#include "Common/BfmeGame.h"

#include "GameClient/Drawable.h"
#include "GameLogic/Object/PristinePose.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace
{
// one prototype's triangles in model space (the hierarchy's bind pose), with the sub object each belongs to and a bounding sphere
struct ModelTriangles
{
	std::vector<float> xyz;      // 9 floats per triangle
	std::vector<int> subObject;  // per triangle
	float centre[3] = { 0.0f, 0.0f, 0.0f };
	float radius = 0.0f;
	// lane SELECT-1: the first oriented box of the top LOD (RotWK HLodClass::Cast_Ray, RW 0x59C680: with the pick flag (+0x42) set, the first top-LOD
	// child of class 0x1B (OBBoxRenderObjClass) answers the ray alone); its pivot's bind-pose transform, centre and half extents in that pivot's space
	int boxSubObject = -1;
	int boxBone = 0;
	Matrix3D boxPivot;
	float boxCentre[3] = { 0.0f, 0.0f, 0.0f };
	float boxExtent[3] = { 0.0f, 0.0f, 0.0f };
};

std::mutex g_cacheMutex;
std::map<const RenderObjPrototype *, std::shared_ptr<const ModelTriangles>> g_cache; // keyed by the asset manager's prototype (client cache, never hashed)

void transformPoint(const Matrix3D &m, float x, float y, float z, float *out)
{
	for (int r = 0; r < 3; ++r)
	{
		out[r] = m.Row[r][0] * x + m.Row[r][1] * y + m.Row[r][2] * z + m.Row[r][3];
	}
}

std::shared_ptr<const ModelTriangles> buildTriangles(const RenderObjPrototype &proto)
{
	auto out = std::make_shared<ModelTriangles>();
	const HTreeClass *tree = proto.Tree ? proto.Tree : &proto.DefaultTree;
	HTreePose pose;
	tree->Base_Pose(Matrix3D(), pose);
	const int pivots = (int)pose.Transform.size();
	auto pivot = [&](int i) -> const Matrix3D & {
		static const Matrix3D identity;
		return i >= 0 && i < pivots ? pose.Transform[(size_t)i] : identity;
	};
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	for (size_t s = 0; s < proto.SubObjects.size(); ++s)
	{
		const RenderSubObject &sub = proto.SubObjects[s];
		// BFME2 BoxPrototypeClass::Create (open-bfme-2 boxrobj.cpp): W3D_BOX_ATTRIBUTE_ORIENTED (1) makes an OBBoxRenderObjClass, else an AABox (class 0x1A),
		// which the pick flag does not prefer. The aggregates are not searched (RW 0x59C680 walks Lod[LodCount - 1] only)
		if (out->boxSubObject < 0 && sub.Type == RenderSubObject::SUB_BOX && sub.Box && !sub.Aggregate && (sub.Box->Attributes & 1u) != 0)
		{
			out->boxSubObject = (int)s;
			out->boxBone = sub.BoneIndex;
			out->boxPivot = pivot(sub.BoneIndex);
			const W3dBoxStruct &b = *sub.Box;
			out->boxCentre[0] = b.Center.X, out->boxCentre[1] = b.Center.Y, out->boxCentre[2] = b.Center.Z;
			out->boxExtent[0] = b.Extent.X, out->boxExtent[1] = b.Extent.Y, out->boxExtent[2] = b.Extent.Z;
		}
		const MeshModelClass *mesh = sub.Type == RenderSubObject::SUB_MESH ? sub.Mesh : nullptr;
		if (!mesh || mesh->Is_Hidden())
		{
			continue;
		}
		const bool skin = mesh->Is_Skin() && mesh->Influences.size() == mesh->Vertices.size();
		std::vector<float> world(mesh->Vertices.size() * 3);
		for (size_t v = 0; v < mesh->Vertices.size(); ++v)
		{
			const W3dVectorStruct &p = mesh->Vertices[v];
			float *w = &world[v * 3];
			if (!skin)
			{
				transformPoint(pivot(sub.BoneIndex), p.X, p.Y, p.Z, w);
				continue;
			}
			// a skin's vertex is in its bone's space; BFME's second bone (Vertices2, weights in percent) blends in when it carries weight
			const W3dVertInfStruct &inf = mesh->Influences[v];
			transformPoint(pivot(inf.BoneIdx), p.X, p.Y, p.Z, w);
			if (inf.Weight1 > 0 && mesh->Vertices2.size() == mesh->Vertices.size())
			{
				float second[3];
				const W3dVectorStruct &q = mesh->Vertices2[v];
				transformPoint(pivot(inf.Bone1Idx), q.X, q.Y, q.Z, second);
				const float sum = (float)inf.Weight0 + (float)inf.Weight1;
				const float a = sum > 0.0f ? (float)inf.Weight0 / sum : 1.0f;
				for (int k = 0; k < 3; ++k)
				{
					w[k] = w[k] * a + second[k] * (1.0f - a);
				}
			}
		}
		for (const W3dTriStruct &tri : mesh->Triangles)
		{
			if (tri.Vindex[0] >= mesh->Vertices.size() || tri.Vindex[1] >= mesh->Vertices.size() || tri.Vindex[2] >= mesh->Vertices.size())
			{
				continue;
			}
			for (int c = 0; c < 3; ++c)
			{
				const float *w = &world[(size_t)tri.Vindex[c] * 3];
				for (int k = 0; k < 3; ++k)
				{
					out->xyz.push_back(w[k]);
					lo[k] = std::min(lo[k], w[k]);
					hi[k] = std::max(hi[k], w[k]);
				}
			}
			out->subObject.push_back((int)s);
		}
	}
	if (!out->subObject.empty())
	{
		float r2 = 0.0f;
		for (int k = 0; k < 3; ++k)
		{
			out->centre[k] = (lo[k] + hi[k]) * 0.5f;
		}
		for (size_t i = 0; i < out->xyz.size(); i += 3)
		{
			const float dx = out->xyz[i] - out->centre[0], dy = out->xyz[i + 1] - out->centre[1], dz = out->xyz[i + 2] - out->centre[2];
			r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
		}
		out->radius = std::sqrt(r2);
	}
	return out;
}

// the asset manager destroys a prototype: its cached triangles go (a later prototype may be allocated at the same address)
void evictPrototype(const RenderObjPrototype *proto)
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	g_cache.erase(proto);
}

std::shared_ptr<const ModelTriangles> trianglesOf(const RenderObjPrototype &proto)
{
	static const bool observing = (W3DSetPrototypeEvictObserver(&evictPrototype), true);
	(void)observing;
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	auto it = g_cache.find(&proto);
	if (it != g_cache.end())
	{
		return it->second;
	}
	std::shared_ptr<const ModelTriangles> t = buildTriangles(proto);
	g_cache.emplace(&proto, t);
	return t;
}

// Moller-Trumbore, both faces: the ray parameter of the hit, false for a miss
bool rayTriangle(const float *o, const float *d, const float *a, const float *b, const float *c, float &t)
{
	const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
	const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (std::fabs(det) < 1e-12f)
	{
		return false;
	}
	const float inv = 1.0f / det;
	const float s[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
	const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
	if (u < 0.0f || u > 1.0f)
	{
		return false;
	}
	const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
	const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
	if (v < 0.0f || u + v > 1.0f)
	{
		return false;
	}
	t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
	return t >= 0.0f;
}

// RotWK OBBoxRenderObjClass::Cast_Ray (RW 0x575220: the collision type test, not hidden, not StartBad, then the ray against the world box RW 0x00B28A30):
// the model-space ray against the box on its pivot (slabs in the pivot's frame; the pivot is a rotation plus a translation)
bool rayBox(const ModelTriangles &m, const Matrix3D &p, const float *o, const float *d, float &t)
{
	float ol[3], dl[3];
	for (int c = 0; c < 3; ++c)
	{
		ol[c] = p.Row[0][c] * (o[0] - p.Row[0][3]) + p.Row[1][c] * (o[1] - p.Row[1][3]) + p.Row[2][c] * (o[2] - p.Row[2][3]);
		dl[c] = p.Row[0][c] * d[0] + p.Row[1][c] * d[1] + p.Row[2][c] * d[2];
	}
	float tin = 0.0f, tout = 1e30f;
	for (int c = 0; c < 3; ++c)
	{
		const float lo = m.boxCentre[c] - m.boxExtent[c], hi = m.boxCentre[c] + m.boxExtent[c];
		if (std::fabs(dl[c]) < 1e-12f)
		{
			if (ol[c] < lo || ol[c] > hi)
			{
				return false;
			}
			continue;
		}
		float a = (lo - ol[c]) / dl[c], b = (hi - ol[c]) / dl[c];
		if (a > b)
		{
			std::swap(a, b);
		}
		tin = std::max(tin, a);
		tout = std::min(tout, b);
		if (tin > tout)
		{
			return false;
		}
	}
	t = tin;
	return true;
}
} // namespace

namespace DrawablePick
{
Result rayTest(const Drawable &drawable, const Coord3D &o, const Coord3D &d, float *t)
{
	bool drawn = false, unknown = false, hit = false;
	float best = 0.0f;
	const float *basis = drawable.getBasis();
	const float scale = drawable.getInstanceScale();
	for (const DrawEntry &e : drawable.entries())
	{
		if (e.moduleHidden || e.conditionHidden || e.missingModel)
		{
			continue;
		}
		const RenderObjPrototype *proto = nullptr;
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
			proto = e.draw->currentModel();
		else if (e.kind == W3D_DRAWKIND_FLOOR && !e.staticModel.empty() && bfme1Mounted())
		{
			// community PR #36, BFME1 only (docs/BFME1.md): BFME1's building plots expose their clickable surface through W3DFloorDraw. RotWK / BFME2
			// floors stay unpickable (S-1200)
			std::string error;
			proto = drawable.drawServices().assets.Create_Render_Obj(e.staticModel, &error);
		}
		else
			continue; // batched trees/props and effect draws are not pick targets
		if (!proto)
		{
			unknown = unknown || (e.draw && e.draw->currentModelState() == nullptr);
			continue;
		}
		const std::shared_ptr<const ModelTriangles> tris = trianglesOf(*proto);
		if (tris->subObject.empty() && tris->boxSubObject < 0)
		{
			continue;
		}
		drawn = true;
		// the ray in model space: world = basis * (scale * model) + position (Thing basis: row-major, columns X, Y, Z)
		const Coord3D at = drawable.entryPosition(e);
		const float rel[3] = { o.x - at.x, o.y - at.y, o.z - at.z };
		const float dw[3] = { d.x, d.y, d.z };
		float om[3], dm[3];
		const float inv = scale != 0.0f ? 1.0f / scale : 1.0f;
		for (int c = 0; c < 3; ++c)
		{
			// the basis is a rotation: its transpose is its inverse
			om[c] = (basis[0 * 3 + c] * rel[0] + basis[1 * 3 + c] * rel[1] + basis[2 * 3 + c] * rel[2]) * inv;
			dm[c] = (basis[0 * 3 + c] * dw[0] + basis[1 * 3 + c] * dw[1] + basis[2 * 3 + c] * dw[2]) * inv;
		}
		static const std::set<int> noHidden;
		const std::set<int> &hidden = e.draw ? e.draw->hiddenSubObjects() : noHidden;
		if (tris->boxSubObject >= 0)
		{
			// lane SELECT-1: the box answers alone (RW 0x59C680 returns the box's Cast_Ray, hit or not; a hidden box misses: RW 0x575220's vslot 0x198)
			// the box follows its bone (HLod::Update_Sub_Object_Transforms, RW 0x59C680's first call): a box on an animated bone (Helm's Deep's gate door,
			// bone 10) is posed with the draw's track-0 animation at its frame; a root-bone box keeps the cached bind pose
			Matrix3D pivotNow = tris->boxPivot;
			if (tris->boxBone > 0 && e.draw)
			{
				W3DDrawPoseView view;
				e.draw->poseView(view);
				const HTreeClass *tree = proto->Tree ? proto->Tree : &proto->DefaultTree;
				std::vector<Matrix3D> pose;
				if (view.clipKey0 && PristinePose::evaluate(*tree, static_cast<const HAnimClass *>(view.clipKey0), (int)view.frame0, 1.0f, pose, nullptr)
					&& tris->boxBone < (int)pose.size())
				{
					pivotNow = pose[(size_t)tris->boxBone];
				}
			}
			float th;
			if (!hidden.count(tris->boxSubObject) && rayBox(*tris, pivotNow, om, dm, th) && (!hit || th < best))
			{
				hit = true;
				best = th;
			}
			continue;
		}
		// the bounding sphere first
		const float oc[3] = { om[0] - tris->centre[0], om[1] - tris->centre[1], om[2] - tris->centre[2] };
		const float a = dm[0] * dm[0] + dm[1] * dm[1] + dm[2] * dm[2];
		const float b = 2.0f * (oc[0] * dm[0] + oc[1] * dm[1] + oc[2] * dm[2]);
		const float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - tris->radius * tris->radius;
		if (a <= 0.0f || b * b - 4.0f * a * c < 0.0f)
		{
			continue;
		}
		const size_t count = tris->subObject.size();
		for (size_t i = 0; i < count; ++i)
		{
			if (!hidden.empty() && hidden.count(tris->subObject[i]))
			{
				continue;
			}
			const float *v = &tris->xyz[i * 9];
			float th;
			if (rayTriangle(om, dm, v, v + 3, v + 6, th) && (!hit || th < best))
			{
				hit = true;
				best = th; // the same parameter as in world space: the map from model to world is linear in t
			}
		}
	}
	if (hit)
	{
		*t = best;
		return Result::Hit;
	}
	if (drawn)
	{
		return Result::Miss;
	}
	return unknown ? Result::Unknown : Result::NotDrawn;
}

const char *stopLine()
{
	return "[S-1200] picking (InGameHud): the ray meets the drawn W3D model's oriented bounding box when its top LOD has one (RotWK RW 0x59C680 / 0x575220, lane "
		   "SELECT-1), else its triangles; a box on an animated bone is posed with the draw's track-0 animation frame (no blend), the triangles stand on the hierarchy's bind pose, mesh translucency is not "
		   "tested, both faces; trees, props and floors pick nothing";
}

size_t cachedModels()
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	return g_cache.size();
}
} // namespace DrawablePick
